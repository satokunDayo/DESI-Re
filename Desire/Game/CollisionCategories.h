#pragma once
#include <cstdint>

// 描画順ではなく「誰と誰を当てるか」の番号。SceneとEnemyで同じ定義を使う。
// categoryは自分の種類、maskは判定したい相手の種類。双方の許可が必要。
namespace GameCollision
{
    inline constexpr std::uint32_t Player = 1u << 0;
    inline constexpr std::uint32_t Wall   = 1u << 1;
    inline constexpr std::uint32_t Goal   = 1u << 2;
    inline constexpr std::uint32_t Enemy  = 1u << 3;
}
