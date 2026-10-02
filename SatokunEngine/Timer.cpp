// 前の時刻を覚え、今との差を秒で出す。それ以上のゲーム進行判断はEngineの仕事。
#include "Timer.h"

// 前回時刻を今にそろえ、差分をゼロに戻す。世界の歴史までは巻き戻せない。
void Timer::Reset()
{
    previous_ = Clock::now();
    deltaSeconds_ = 0.0;
}

// 「時よ止まれッ！ザ・ワールド!! えっほえっほ...処理しないと....そして時は動き出すを毎フレームやる
void Timer::Tick()
{
    // 今の時間を観測
    const auto now = Clock::now();

    // 前回観測した時間との「差分（デルタ）」を算出。
    // 整数の秒にすると短い1フレームが0になってしまうので、小数を持てるdoubleへ変換。
    deltaSeconds_ =
        std::chrono::duration<double>(now - previous_).count();

    // 「今」を「過去」として記憶に刻み、次のフレームの計測へ備える。
    previous_ = now;
}
// （※補足：古いPCゲームで、CPUが良すぎるとキャラが爆速で動いてまともに遊べなくなるアレを防ぐための最重要処理です）