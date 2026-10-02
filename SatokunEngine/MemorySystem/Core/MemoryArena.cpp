// 「閉じたい」と「もう閉じてよい」を分ける実装。参照が残るなら、その場で待たず次回へ回す。
#include "MemoryArena.h"
#include <atomic>
#include <cassert>
#include <utility>

// Arenaごとに識別番号を発行する。番号の採番だけなので他データの同期は要らない。
namespace { std::atomic<std::uint64_t> nextArenaId{1}; }

MemoryBlock::MemoryBlock(std::shared_ptr<MemoryArena> arena, void* data, std::size_t size)
    : arena_(std::move(arena)), data_(data), size_(size) {}
// RAIIの出口。通常終了も例外も、持っているブロックを返す経路は同じ。
MemoryBlock::~MemoryBlock() { Reset(); }
// move元を空にするから、2人が同じ領域を返す事故を防げる。
MemoryBlock::MemoryBlock(MemoryBlock&& other) noexcept
    : arena_(std::move(other.arena_)), data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}
// 自分の今の借り物を返してから、相手の借り物を引き受ける。自己代入は無視。
MemoryBlock& MemoryBlock::operator=(MemoryBlock&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        arena_ = std::move(other.arena_);
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}
// 先に自分を空にし、ownerをローカルに残して返却先の寿命を守る。
void MemoryBlock::Reset() noexcept
{
    auto owner = std::move(arena_);
    void* data = std::exchange(data_, nullptr);
    size_ = 0;
    if (owner) owner->ReturnBlock(data);
}

ArenaOperation::ArenaOperation(std::shared_ptr<MemoryArena> arena)
    : arena_(std::move(arena)) {}
// 作業札をスコープの終わりで返す。例外で途中退場しても作業数を残さない。
ArenaOperation::~ArenaOperation() { Reset(); }
ArenaOperation::ArenaOperation(ArenaOperation&& other) noexcept
    : arena_(std::move(other.arena_)) {}
ArenaOperation& ArenaOperation::operator=(ArenaOperation&& other) noexcept
{
    if (this != &other) { Reset(); arena_ = std::move(other.arena_); }
    return *this;
}
// 空の札なら何もしない。move済みの札を片付けても二重に減らない。
void ArenaOperation::Reset() noexcept
{
    auto owner = std::move(arena_);
    if (owner) owner->EndOperation();
}

// 大元のBlockAllocatorをここで作る。Createを通さない外部生成はできない。
MemoryArena::MemoryArena(std::size_t capacity)
    : id_(nextArenaId.fetch_add(1, std::memory_order_relaxed)),
      allocator_(std::make_unique<BlockAllocator>(capacity)) {}

// コンストラクタがprivateなので、クラスの中からnewしてshared_ptrへ渡す。
std::shared_ptr<MemoryArena> MemoryArena::Create(std::size_t capacity)
{
    return std::shared_ptr<MemoryArena>(new MemoryArena(capacity));
}

// 受付状態を確認してから確保し、このArenaの所有も一緒にBlockへ渡す。
MemoryBlock MemoryArena::Allocate(std::size_t bytes, std::size_t alignment)
{
    std::lock_guard lock(mutex_);
    if (state_ != ArenaState::Open) return {};
    auto owner = shared_from_this();
    void* data = allocator_->Allocate(bytes, alignment);
    return data ? MemoryBlock(std::move(owner), data, bytes) : MemoryBlock{};
}

// 受付中だけ作業を増やす。ロード開始とシーン終了が同時に来ても順番を決める。
ArenaOperation MemoryArena::BeginOperation()
{
    std::lock_guard lock(mutex_);
    if (state_ != ArenaState::Open) return {};
    auto owner = shared_from_this();
    ++operations_;
    return ArenaOperation(std::move(owner));
}

// 閉店中でも返却は受け付ける。ここを止めたら一生閉店できない。
void MemoryArena::ReturnBlock(void* data) noexcept
{
    std::lock_guard lock(mutex_);
    assert(allocator_);
    [[maybe_unused]] const bool returned = allocator_->Free(data);
    assert(returned);
}

// 札の返却で作業数を減らす。データの解放数とは別カウント。
void MemoryArena::EndOperation() noexcept
{
    std::lock_guard lock(mutex_);
    assert(operations_ > 0);
    --operations_;
}

// 状態をロックして読む。trueの直後に閉店する可能性はあるので、Allocateでも再確認する。
bool MemoryArena::IsOpen() const
{
    std::lock_guard lock(mutex_);
    return state_ == ArenaState::Open;
}

// 新規受付だけ止める。既存のBlockやPool Handleはまだ使える。
void MemoryArena::BeginClose()
{
    std::lock_guard lock(mutex_);
    if (state_ == ArenaState::Open) state_ = ArenaState::Closing;
}

// Arenaが閉じ始めていたら新しいプールは登録させない。古いweakの抜け殻も掃除。
bool MemoryArena::RegisterPool(const std::shared_ptr<PoolControl>& pool)
{
    std::lock_guard lock(mutex_);
    if (state_ != ArenaState::Open) return false;
    // Remove expired weak registrations before adding another pool.
    std::erase_if(pools_, [](const auto& value) { return value.expired(); });
    pools_.push_back(pool);
    return true;
}

// プールへ閉店を頼む→借り物と作業を数える→全部ゼロなら大元を返す、の順。
bool MemoryArena::TryClose()
{
    std::vector<std::shared_ptr<PoolControl>> pools;
    {
        std::lock_guard lock(mutex_);
        if (state_ == ArenaState::Closed) return true;
        state_ = ArenaState::Closing;
        pools.reserve(pools_.size());
        for (const auto& weak : pools_) if (auto pool = weak.lock()) pools.push_back(std::move(pool));
    }

    // Pool clearing can return allocations; never hold the arena lock here.
    // この時点ではArenaのロックを外している。PoolがBlockを返すと同じArenaへ戻ってくるため。
    bool poolsReady = true;
    for (const auto& pool : pools) if (!pool->TryClear()) poolsReady = false;

    std::lock_guard lock(mutex_);
    if (state_ == ArenaState::Closed) return true;
    if (!poolsReady || operations_ != 0 || allocator_->GetStats().activeAllocations != 0)
        return false;
    // ここで初めて貸出用の大きな領域も解放。通常のBlock返却とは段階が違う。
    allocator_.reset();
    pools_.clear();
    state_ = ArenaState::Closed;
    return true;
}

// ClosedならAllocatorはもうない。空の統計で返して、消えた相手に聞きに行かない。
MemoryArena::Snapshot MemoryArena::GetSnapshot() const
{
    std::lock_guard lock(mutex_);
    return {id_, state_, allocator_ ? allocator_->GetStats() : BlockAllocator::Stats{}, operations_};
}
