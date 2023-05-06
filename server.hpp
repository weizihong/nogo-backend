//
// chat_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once
#define export

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <chrono>
#include <deque>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "contest.hpp"
#include "message.hpp"
#include "uimessage.hpp"

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;
using asio::ip::tcp;

class Room {
    Contest contest;
    std::deque<std::string> chats;

public:
    Room(asio::io_context& io_context)
        : timer_ { io_context }
    {
    }
    /*
    建立连接成功时，客户端是请求对局方，也就是服务端在收到客户端任何通信前不与客户端进行交流。
    对局结束后，或暂时拒绝后，想再来一局（重发）的是请求对局方。另外，如果已经收到对方想再来一局了，程序应该阻止本方重复发送请求，而引导先接受或拒绝。你可以不处理因信息传输导致的同时发送，你可以假设不存在这种情况。
    */
    void process_data(Message msg, Participant_ptr participant)
    {
        string_view data1 { msg.data1 }, data2 { msg.data2 };

        switch (msg.op) {
        case OpCode::UPDATE_UI_STATE_OP: {
            break;
        }
        case OpCode::START_LOCAL_GAME_OP: {
            // TODO
            if (contest.status == Contest::Status::GAME_OVER) {
                contest.clear();
            }
            Player player1 { participant, "BLACK", Role::BLACK, PlayerType::LOCAL_HUMAN_PLAYER },
                player2 { participant, "WHITE", Role::WHITE, PlayerType::LOCAL_HUMAN_PLAYER };
            contest.enroll(player1), contest.enroll(player2);

            if (participant->is_local) {
                participant->deliver(UiMessage(contest));
            }

            break;
        }
        case OpCode::LOCAL_GAME_TIMEOUT_OP: {
            auto role { data1 == "b" ? Role::BLACK : data1 == "w" ? Role::WHITE
                                                                  : Role::NONE };
            auto player { contest.players.at(role, participant) };

            contest.timeout(player);

            if (participant->is_local) {
                participant->deliver(UiMessage(contest));
            }

            break;
        }

        case OpCode::READY_OP: {
            Role role { data2 == "b" ? Role::BLACK : data2 == "w" ? Role::WHITE
                                                                  : Role::NONE }; // or strict?
            Player player { participant, data1, role, PlayerType::REMOTE_HUMAN_PLAYER };
            contest.enroll(player);
            break;
        }
        case OpCode::REJECT_OP: {
            contest.reject();
            break;
        }
        case OpCode::MOVE_OP: {
            timer_.cancel();

            Position pos { data1[0] - 'A', data1[1] - '1' }; // 11-way board will fail!
            // auto role { data2 == "b" ? Role::BLACK : data2 == "w" ? Role::WHITE
            //                                                      : Role::NONE };

            milliseconds ms { std::stoull(std::string { data2 }) };

            auto player { contest.players.at(Role::NONE, participant) };
            auto opponent { contest.players.at(-player.role) };

            contest.play(player, pos);

            if (participant->is_local) {
                participant->deliver(UiMessage(contest));
            }

            timer_.expires_after(TIMEOUT);
            timer_.async_wait([this, opponent](const asio::error_code& ec) {
                if (!ec) {
                    contest.timeout(opponent);
                    opponent.participant->deliver({ OpCode::TIMEOUT_END_OP });
                }
            });

            if (contest.result.winner == opponent.role) {
                participant->deliver({ OpCode::SUICIDE_END_OP });
                deliver(msg, participant); // broadcast
            } else if (contest.result.winner == player.role) {
                participant->deliver({ OpCode::GIVEUP_OP });
                opponent.participant->deliver({ OpCode::GIVEUP_END_OP }); // radical
            }
            break;
        }
        case OpCode::GIVEUP_OP: {
            auto role { data2 == "b" ? Role::BLACK : data2 == "w" ? Role::WHITE
                                                                  : Role::NONE };
            auto player { contest.players.at(role, participant) };

            contest.concede(player);

            if (participant->is_local) {
                participant->deliver(UiMessage(contest));
            }
            break;
        }
        case OpCode::TIMEOUT_END_OP:
        case OpCode::SUICIDE_END_OP:
        case OpCode::GIVEUP_END_OP:
            // should not receive this code
            // game over signal should send by me
            // the evil client must be wrong
            break;

        case OpCode::LEAVE_OP: {
            participant->stop();
            break;
        }
        case OpCode::CHAT_OP: {
            recent_msgs_.push_back(msg);
            while (recent_msgs_.size() > max_recent_msgs)
                recent_msgs_.pop_front();

            for (auto participant : participants_)
                participant->deliver(msg);
            break;
        }
        }
    }
    void join(Participant_ptr participant)
    {
        participants_.insert(participant);
        for (auto msg : recent_msgs_) {
            participant->deliver(msg);
        }
    }

    void leave(Participant_ptr participant)
    {
        participants_.erase(participant);
    }

    void deliver(Message msg, Participant_ptr participant)
    {
        recent_msgs_.push_back(msg);
        while (recent_msgs_.size() > max_recent_msgs)
            recent_msgs_.pop_front();

        for (auto p : participants_)
            if (p != participant)
                participant->deliver(msg);
    }

private:
    asio::steady_timer timer_;

    std::set<Participant_ptr> participants_;
    enum { max_recent_msgs = 100 };
    std::deque<Message> recent_msgs_;
};

class Session : public Participant, public std::enable_shared_from_this<Session> {
public:
    tcp::endpoint endpoint() const override
    {
        return socket_.remote_endpoint();
    }
    bool operator==(const Participant& participant) const override
    {
        return endpoint() == participant.endpoint();
    }
    Session(tcp::socket socket, Room& room, bool is_local = false)
        : Participant { is_local }
        , socket_(std::move(socket))
        , timer_(socket_.get_executor())
        , room_(room)
    {
        timer_.expires_at(std::chrono::steady_clock::time_point::max());
    }

    void start()
    {
        room_.join(shared_from_this());

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->reader(); }, detached);

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->writer(); }, detached);
    }

    void deliver(Message msg) override
    {
        std::cout << "deliver " << msg.to_string() << std::endl;
        write_msgs_.push_back(msg);
        timer_.cancel_one();
    }

    void stop() override
    {
        room_.leave(shared_from_this());
        socket_.close();
        timer_.cancel();
    }

private:
    awaitable<void> reader()
    {
        try {
            for (std::string read_msg;;) {
                std::size_t n = co_await asio::async_read_until(socket_, asio::dynamic_buffer(read_msg, 1024), "\n", use_awaitable);

                Message msg { read_msg.substr(0, n) };
                room_.process_data(msg, shared_from_this());

                read_msg.erase(0, n);
            }
        } catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << "\n";
            stop();
        }
    }

    awaitable<void> writer()
    {
        try {
            while (socket_.is_open()) {
                if (write_msgs_.empty()) {
                    asio::error_code ec;
                    co_await timer_.async_wait(redirect_error(use_awaitable, ec));
                } else {
                    co_await asio::async_write(socket_, asio::buffer(write_msgs_.front().to_string() + "\n"),
                        use_awaitable);
                    write_msgs_.pop_front();
                }
            }
        } catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << "\n";
            stop();
        }
    }

    tcp::socket socket_;
    asio::steady_timer timer_;
    Room& room_;
    std::deque<Message> write_msgs_;
};

awaitable<void> listener(tcp::acceptor acceptor, bool is_local = false)
{
    // This is safe because the acceptor was created with an io_context object
    asio::execution_context& ec = acceptor.get_executor().context();
    asio::io_context& io_context = static_cast<asio::io_context&>(ec);

    Room room { io_context };

    for (;;) {
        std::make_shared<Session>(co_await acceptor.async_accept(use_awaitable), room, is_local)->start();
    }
}

export void launch_server(std::vector<asio::ip::port_type> ports)
{
    try {
        asio::io_context io_context(1);

        co_spawn(io_context, listener(tcp::acceptor(io_context, { tcp::v4(), ports[0] }), true), detached);
        for (auto port : ports | std::views::drop(1)) {
            co_spawn(io_context, listener(tcp::acceptor(io_context, { tcp::v4(), port })), detached);
        }

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) { io_context.stop(); });

        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}