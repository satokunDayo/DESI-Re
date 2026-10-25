#pragma once
// Scene用/System用の領域と、その寿命をつなぐ仕組み。借り物が残る間は店じまいを待つ。
#include "MemorySystem/Core/BlockAllocator.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class MemoryArena;

// Move-only ownership of one allocation. The arena outlives this handle.
// 1ブロックの返却係。コピーは禁止、moveで持ち主を移せる。消えれば自動で返す。
class MemoryBlock final
{
public:
    MemoryBlock() = default;
    ~MemoryBlock();
    MemoryBlock(MemoryBlock&& other) noexcept;
    MemoryBlock& operator=(MemoryBlock&& other) noexcept;
    MemoryBlock(const MemoryBlock&) = delete;
    MemoryBlock& operator=(const MemoryBlock&) = delete;
    // ここで領域を返す。配置したC++オブジェクトのデストラクタは自動では呼ばない。
    void Reset() noexcept;
    // 生ポインタは借り物。MemoryBlockをResetした後に使うと住所だけ残った廃屋になる。
    void* Data() const noexcept { return data_; }
    std::size_t Size() const noexcept { return size_; }
    explicit operator bool() const noexcept { return data_ != nullptr; }

private:
    friend class MemoryArena;
    MemoryBlock(std::shared_ptr<MemoryArena> arena, void* data, std::size_t size);
    // 貸したArenaを生かしておくための所有。ManagerよりBlockが長生きしても返却先は残る。
    std::shared_ptr<MemoryArena> arena_;
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

// Keeps an arena busy during an operation that may not yet own a buffer.
// まだバッファを持っていない処理にも「作業中」の札を付ける。スレッドを作る機能ではない。
class ArenaOperation final
{
public:
    ArenaOperation() = default;
    ~ArenaOperation();
    ArenaOperation(ArenaOperation&& other) noexcept;
    ArenaOperation& operator=(ArenaOperation&& other) noexcept;
    ArenaOperation(const ArenaOperation&) = delete;
    ArenaOperation& operator=(const ArenaOperation&) = delete;
    void Reset() noexcept;
    explicit operator bool() const noexcept { return arena_ != nullptr; }

private:
    friend class MemoryArena;
    explicit ArenaOperation(std::shared_ptr<MemoryArena> arena);
    std::shared_ptr<MemoryArena> arena_;
};

// Tの種類を知らなくてもArenaからプールへ閉店を頼める共通窓口。
class PoolControl
{
public:
    virtual ~PoolControl() = default;
    virtual bool TryClear() = 0;
};

// Open=受付中、Closing=新規受付停止、Closed=領域返却済み。閉店中も既存の借り物は有効。
enum class ArenaState { Open, Closing, Closed };

class MemoryArena final : public std::enable_shared_from_this<MemoryArena>
{
public:
    // ある瞬間の状態のコピー。呼び出した後で他スレッドが返却すれば、実態はもう変わり得る。
    struct Snapshot
    {
        std::uint64_t id = 0;
        ArenaState state = ArenaState::Closed;
        BlockAllocator::Stats blocks;
        std::size_t inFlightOperations = 0;
    };

    // shared_from_thisを使うので、Arenaはこの入口でshared_ptrに載せて作る。
    static std::shared_ptr<MemoryArena> Create(std::size_t capacity);
    [[nodiscard]] MemoryBlock Allocate(std::size_t bytes,
        std::size_t alignment = alignof(std::max_align_t));
    [[nodiscard]] ArenaOperation BeginOperation();
    bool IsOpen() const;
    void BeginClose();
    // 返却が済んだか試すだけ。借り物や作業が残る間はfalseで、強制破壊しない。
    [[nodiscard]] bool TryClose();
    Snapshot GetSnapshot() const;
    std::uint64_t GetId() const noexcept { return id_; }
    bool RegisterPool(const std::shared_ptr<PoolControl>& pool);

private:
    friend class MemoryBlock;
    friend class ArenaOperation;
    explicit MemoryArena(std::size_t capacity);
    void ReturnBlock(void* data) noexcept;
    void EndOperation() noexcept;

    // 同じScene容量でも作り直したら別ID。古いキャッシュと新しいシーンを混ぜないため。
    const std::uint64_t id_;
    mutable std::mutex mutex_;
    ArenaState state_ = ArenaState::Open;
    std::unique_ptr<BlockAllocator> allocator_;
    // プールの目録はweak。ArenaとPoolがお互いを強く所有すると、永久に成仏できなくなる。
    std::vector<std::weak_ptr<PoolControl>> pools_;
    std::size_t operations_ = 0;
};
