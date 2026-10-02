// 画像表示から切り離したイベント進行。先にこの順番を固めれば、後から文字描画を交換できる。
#include "EventSequence.h"
#include <cmath>
#include <stdexcept>
#include <utility>

// 待ち時間を検証してから差し替える。おかしなページで再生状態だけ半端に壊さない。
void EventSequence::Start(std::vector<EventPage> pages)
{
    for (const auto& page : pages)
        if (!std::isfinite(page.minimumSeconds) || page.minimumSeconds < 0.0)
            throw std::invalid_argument("Event wait must be finite and nonnegative.");
    pages_ = std::move(pages);
    index_ = 0;
    elapsed_ = 0.0;
    status_ = pages_.empty() ? EventStatus::Completed : EventStatus::Running;
}

// 止まっているなら何もしない。skipは待機時間やconfirmより優先する。
void EventSequence::Update(double seconds, bool confirm, bool skip)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        throw std::invalid_argument("Invalid event delta time.");
    if (status_ != EventStatus::Running) return;
    if (skip) { status_ = EventStatus::Skipped; return; }
    const auto& page = pages_[index_];
    // 経過を待ち時間で飽和させる。1回の入力で複数ページを送らない。
    // 待ち終わった後も経過時間を際限なく足さず、最低待機秒数で止めておく。
    const double remaining = page.minimumSeconds - elapsed_;
    elapsed_ = seconds >= remaining ? page.minimumSeconds : elapsed_ + seconds;
    if (elapsed_ < page.minimumSeconds || (page.requireConfirm && !confirm)) return;
    // 次のページへ進み、待ち時間は0から。余った時間や同じ入力を次へ持ち越さない。
    ++index_;
    elapsed_ = 0.0;
    if (index_ == pages_.size()) status_ = EventStatus::Completed;
}

// 完走済み・スキップ済みの結果を、あとからCancelledで上書きしない。
void EventSequence::Cancel() noexcept
{
    if (status_ == EventStatus::Running) status_ = EventStatus::Cancelled;
}

// 再生中だけ有効なページを返す。最後のページを越えたindexで配列を読まない。
const EventPage* EventSequence::GetCurrentPage() const noexcept
{
    return status_ == EventStatus::Running ? &pages_[index_] : nullptr;
}
