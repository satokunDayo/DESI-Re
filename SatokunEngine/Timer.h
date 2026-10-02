#pragma once
// 時計係。経過時間を測るだけで、SleepもFPS制限も担当しない。
#include <chrono>

// もちろん final で継承を封じる。
class Timer final
{
public:
    // 時の再始動と更新の儀式（詳細はcppにて）
    // 前回時刻を今に戻す。ゲームループを始める直前に呼ぶ。
    void Reset();
    // 前回から何秒たったかを更新。GetDeltaSecondsはこの計測結果を見るだけ。
    void Tick();

    // デルタタイム（経過時間）を覗き見る constだから一人で抱え込む系ヒロイン。俺でよかったら話聞くy
    double GetDeltaSeconds() const { return deltaSeconds_; }

private:
    // steady_clock を利用、絶対に時を刻み続ける子。俺が付き添ってあげr
    // PCの時計を手で変更しても巻き戻らない経過時間用の時計を使う。
    using Clock = std::chrono::steady_clock;

    // 現在時刻を次フレームの比較のために保存
    Clock::time_point previous_ = Clock::now();

    // 刹那の時間を収める器
    double deltaSeconds_ = 0.0;
};