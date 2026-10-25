#pragma once
#include <cmath>
#include <stdexcept>

// 秒・メートル・度をそのまま指定する。ゲーム側の調整値。
struct EnemyAiSettings
{
    double findIntervalSeconds = 8.0;
    float findErrorRadius = 5.0f;
    double chaseIntervalScale = 0.1; // Findの取得間隔の1/10。
    float chaseErrorScale = 0.2f;    // Findの誤差半径の1/5。
    double findThinkSeconds = 2;   // 計算中にSleepするのではなく、敵だけ待機する。
    float moveSpeed = 2.0f;          // 現在のプレイヤーは2.5m/s。
    float sightDistance = 15.0f;
    float sightAngleDegrees = 80.0f; // 円錐全体の角度。左右それぞれ40度。
    double loseSightSeconds = 10.0;
    int spawnMinPathCells = 20;      // プレイヤーから通路をたどって20マス以上。
    float initialYawDegrees = 270.0f;

    void Validate() const
    {
        if (!std::isfinite(findIntervalSeconds) || findIntervalSeconds <= 0 ||
            !std::isfinite(findErrorRadius) || findErrorRadius < 0 ||
            !std::isfinite(chaseIntervalScale) || chaseIntervalScale <= 0 || chaseIntervalScale > 1 ||
            findIntervalSeconds * chaseIntervalScale < 0.02 ||
            !std::isfinite(chaseErrorScale) || chaseErrorScale < 0 || chaseErrorScale > 1 ||
            !std::isfinite(findThinkSeconds) || findThinkSeconds < 0 || findThinkSeconds >= findIntervalSeconds ||
            !std::isfinite(moveSpeed) || moveSpeed <= 0 ||
            !std::isfinite(sightDistance) || sightDistance <= 0 ||
            !std::isfinite(sightAngleDegrees) || sightAngleDegrees <= 0 || sightAngleDegrees >= 180 ||
            !std::isfinite(loseSightSeconds) || loseSightSeconds < 0 ||
            spawnMinPathCells < 1 || !std::isfinite(initialYawDegrees))
            throw std::invalid_argument("Invalid EnemyAiSettings (seconds, metres or degrees).");
    }
};

namespace EnemyTuning
{
    // C++17以降のinline変数。同じヘッダーを複数cppから読んでも実体は1個。
    // ゲームのメインスレッドから変更する。別スレッドとの同時書き換えはしない。
    inline EnemyAiSettings settings;
}

inline void changeEnemySpeed(float newSpeed)
{
    EnemyTuning::settings.moveSpeed = newSpeed;
}