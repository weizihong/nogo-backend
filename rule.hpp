#pragma once
#ifndef _EXPORT
#define _EXPORT
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <iostream>
#include <ranges>
#include <vector>
#include <nlohmann/json.hpp>

#ifdef __GNUC__
#include <range/v3/all.hpp>
#else
namespace ranges = std::ranges;
#endif

template <typename T>
constexpr auto stoi_base(std::string_view str)
{
    T result;
    auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
    switch (ec) {
    case std::errc::invalid_argument:
        throw std::invalid_argument { "no conversion" };
    case std::errc::result_out_of_range:
        throw std::out_of_range { "out of range" };
    default:
        return result;
    };
}
constexpr auto stoi = stoi_base<int>;
constexpr auto stoull = stoi_base<unsigned long long>;

_EXPORT constexpr inline auto rank_n = 9;

_EXPORT struct Position {
    int x { -1 }, y { -1 };
    constexpr Position(int x, int y)
        : x(x)
        , y(y)
    {
    }
    constexpr Position() = default;
    constexpr Position operator+(Position p) const
    {
        return { x + p.x, y + p.y };
    }
    constexpr explicit operator bool() const { return x >= 0 && y >= 0; }
    constexpr auto operator<=>(const Position& p) const = default;
    auto to_string() const -> std::string
    {
        return std::string(1, 'A' + x) + std::to_string(y + 1);
    }
    constexpr explicit Position(std::string_view str)
        : Position(str[0] - 'A', stoi(str.substr(1)) - 1)
    {
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Position, x, y)
};

_EXPORT struct Role {
    int id;
    static const Role BLACK, WHITE, NONE;

    constexpr Role()
        : Role(0)
    {
    }

    constexpr decltype(auto) map(auto&& v_black, auto&& v_white, auto&& v_none) const
    {
        return id == 1 ? v_black
            : id == -1 ? v_white
                       : v_none;
    }
    constexpr auto operator<=>(const Role&) const = default;
    constexpr auto operator-() const { return Role(-id); }
    constexpr explicit operator bool() { return id; }

    auto to_string() const -> std::string
    {
        return map("BLACK", "WHITE", "NONE");
    }

    explicit constexpr Role(std::string_view str)
        : Role(str == "b"    ? 1
                : str == "w" ? -1
                             : 0)
    {
    }

    explicit constexpr operator int() const { return id; }

private:
    constexpr explicit Role(int id)
        : id(id)
    {
    }
};
constexpr Role Role::BLACK { 1 }, Role::WHITE { -1 }, Role::NONE { 0 };

_EXPORT class Board {
    std::array<Role, rank_n * rank_n> arr;

    static constexpr std::array delta { Position { -1, 0 }, Position { 1, 0 }, Position { 0, -1 }, Position { 0, 1 } };
    auto neighbor(Position p) const
    {
        return delta | std::views::transform([&](auto d) { return p + d; })
            | std::views::filter([&](auto p) { return in_border(p); })
            | ranges::to<std::vector>();
    }

public:
    // constexpr auto operator[](this auto&& self, Position p) { return self.arr[p.x * rank_n + p.y]; }
    constexpr auto operator[](Position p) -> Role& { return arr[p.x * rank_n + p.y]; }
    constexpr auto operator[](Position p) const { return arr[p.x * rank_n + p.y]; }

    constexpr bool in_border(Position p) const { return p.x >= 0 && p.y >= 0 && p.x < rank_n && p.y < rank_n; }

    static constexpr auto index()
    {
        std::array<Position, rank_n * rank_n> res;
        for (int i = 0; i < rank_n; i++)
            for (int j = 0; j < rank_n; j++)
                res[i * rank_n + j] = { i, j };
        return res;
    }

    auto _liberties(Position p, Board& visit) const -> bool
    {
        auto& self { *this };
        visit[p] = Role::BLACK;
        return std::ranges::any_of(neighbor(p), [&](auto n) {
            return !self[n];
        }) || std::ranges::any_of(neighbor(p), [&](auto n) {
            return !visit[n] && self[n] == self[p]
                && _liberties(n, visit);
        });
    };
    bool liberties(Position p) const
    {
        auto& self { *this };
        Board visit {};
        return self._liberties(p, visit);
    }

    // judge whether stones around `p` is captured by `p`
    // or `p` is captured by stones around `p`
    bool is_capturing(Position p) const
    {
        // assert(self[p]);

        auto& self { *this };
        return !self.liberties(p)
            || std::ranges::any_of(neighbor(p), [&](auto n) {
                   return self[n] == -self[p]
                       && !self.liberties(n);
               });
    }

    constexpr auto to_2darray() const
    {
        std::array<std::array<Role, rank_n>, rank_n> res;
        for (int i = 0; i < rank_n; i++)
            for (int j = 0; j < rank_n; j++)
                res[i][j] = arr[i * rank_n + j];
        return res;
    }

    friend auto operator<<(std::ostream& os, const Board& board) -> std::ostream&
    {
        auto arr = board.to_2darray();
        for (int i = 0; i < rank_n; i++) {
            for (int j = 0; j < rank_n; j++)
                os << arr[i][j].map("B", "W", "-");
            os << std::endl;
        }
        return os;
    }
};

_EXPORT struct State {
    Board board;
    Role role;
    Position last_move;

    constexpr State(Role role = Role::BLACK)
        : role(role)
    {
    }
    State(Board board, Role role, Position last_move)
        : board(board)
        , role(role)
        , last_move(last_move)
    {
    }

    auto next_state(Position p) const
    {
        State state { board, -role, p };
        state.board[p] = role;
        return state;
    }

    auto available_actions() const
    {
        auto index = Board::index();
        return index | ranges::views::filter([&](auto pos) {
            return !board[pos] && !next_state(pos).board.is_capturing(pos);
        }) | ranges::to<std::vector>();
    }

    constexpr auto is_over() const
    {
        if (last_move && board.is_capturing(last_move)) // win
            return role;
        /*
        if (!available_actions().size()) // lose
            return -role;
        */
        return Role::NONE;
    }
};