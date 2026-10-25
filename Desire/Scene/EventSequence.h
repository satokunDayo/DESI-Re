#pragma once
// 紙芝居の進行係。今は文章・待ち・送り・スキップまでで、画像や音を描くクラスではない。
#include <cstddef>
#include <string>
#include <vector>

// 1ページ分の設定。文章を見せる最低時間と、決定を待つかをセットにする。
struct EventPage
{
    std::wstring text;
    double minimumSeconds = 0.0;
    bool requireConfirm = true;
};

// 完走とスキップと中止を区別する。どこからでも「終わった」で済ませると後処理が分からなくなる。
enum class EventStatus { Idle, Running, Completed, Skipped, Cancelled };

// 今回は文章/待ち/送り/スキップまで。画像と音の命令は後の描画・音声編で接続。
class EventSequence final
{
public:
    // ページを受け取って0ページ目からやり直す。空の配列ならすぐCompleted。
    void Start(std::vector<EventPage> pages);
    // 1回で進むのは最大1ページ。confirmには「押した瞬間」を渡す。押下中のtrueを毎回渡すと毎回進む。
    void Update(double seconds, bool confirm, bool skip);
    void Cancel() noexcept;
    EventStatus GetStatus() const noexcept { return status_; }
    // 内部ページを借りて読む。次のStartで配列が入れ替わるので、ポインタを持ち歩かない。
    const EventPage* GetCurrentPage() const noexcept;
    std::size_t GetPageIndex() const noexcept { return index_; }

private:
    // ページ本文の持ち主。現在の位置と、そのページで待った秒数は下の2値で管理する。
    std::vector<EventPage> pages_;
    std::size_t index_ = 0;
    double elapsed_ = 0.0;
    EventStatus status_ = EventStatus::Idle;
};
