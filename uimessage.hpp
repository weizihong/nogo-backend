#pragma once
#define export

#include <array>
#include <ctime>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <vector>

#include "contest.hpp"
#include "message.hpp"
#include "rule.hpp"

using nlohmann::json;
using std::string;
using std::string_view;

namespace nlohmann {
template <class T>
void to_json(nlohmann::json& j, const std::optional<T>& v)
{
    if (v.has_value())
        j = *v;
    else
        j = nullptr;
}
template <class T>
void from_json(const nlohmann::json& j, std::optional<T>& v)
{
    if (j.is_null())
        v = std::nullopt;
    else
        v = j.get<T>();
}
}

export struct UiMessage : public Message {
    struct DynamicStatistics {
        string id, name, value;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(DynamicStatistics, id, name, value)
    };
    struct PlayerData {
        string name, avatar;
        PlayerType type;
        int chess_type;
        PlayerData() = default;
        PlayerData(const Player& player)
            : name(player.name)
            , type(PlayerType::LOCAL_HUMAN_PLAYER)
            , chess_type(player.role.id)
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerData, name, avatar, type, chess_type)
    };
    struct GameMetadata {
        int size;
        PlayerData player_opposing, player_our;
        int turn_timeout;
        GameMetadata() = default;
        GameMetadata(const PlayerCouple& players)
            : size(rank_n)
            , player_opposing(PlayerData(players.player2))
            , player_our(PlayerData(players.player1))
            , turn_timeout(0)
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameMetadata, size, player_opposing, player_our, turn_timeout)
    };
    struct Game {
        std::array<std::array<int, rank_n>, rank_n> chessboard;
        bool is_our_player_playing;
        GameMetadata gamemetadata;
        std::vector<DynamicStatistics> statistics;
        Game() = default;
        Game(const Contest& contest)
            : is_our_player_playing(contest.current.role == contest.players.player1.role)
            , gamemetadata(GameMetadata(contest.players))
        {
            for (int i = 0; i < rank_n; i++)
                for (int j = 0; j < rank_n; j++)
                    chessboard[i][j] = contest.current.board.arr[Position { i, j }].id;
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Game, chessboard, is_our_player_playing, gamemetadata, statistics);
    };
    struct UiState {
        bool is_gaming;
        std::optional<Game> game;
        UiState(const Contest& contest)
            : is_gaming(contest.status == Contest::Status::ON_GOING)
            , game(is_gaming ? std::optional<Game>(contest) : std::nullopt)
        {
        }
        auto to_string() -> string
        {
            return json(*this).dump();
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(UiState, is_gaming, game);
    };
    UiMessage(const Contest& contest)
        : Message(OpCode::UPDATE_UI_STATE_OP, std::to_string(std::time(0)), UiState(contest).to_string())
    {
    }
};