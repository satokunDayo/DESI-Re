
#pragma once
// CPUメモリの切り分け係。大きな領域を1個買って、その中の空きページを貸し出す。
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

// CPU suballocator. Thread-safe operations; destruction requires no users.
class BlockAllocator final
{
public:
    // 単位は1024進法のKiB/MiB。Windows全体やVRAMを管理するクラスではない。
    static constexpr std::size_t KiB = 1024;
    static constexpr std::size_t MiB = 1024 * KiB;
    static constexpr std::size_t PageSize = 4 * KiB;
    static constexpr std::size_t MaxAllocationSize = 128 * MiB;
    static constexpr std::size_t MaxAlignment = 128 * MiB;

    // requestedは注文された量、reservedは丸めて席を取った量。差額は丸めの余白。
    struct Stats
    {
        std::size_t capacityBytes = 0;
        std::size_t requestedBytes = 0;
        std::size_t reservedBytes = 0;
        std::size_t activeAllocations = 0;
    };

    // 容量は作成時に固定。256 MiBを確保しても、256 MiB全部が使用中になるわけではない。
    explicit BlockAllocator(std::size_t capacity = 256 * MiB);
    ~BlockAllocator() = default;
    BlockAllocator(const BlockAllocator&) = delete;
    BlockAllocator& operator=(const BlockAllocator&) = delete;

    // nullptr: invalid size/alignment or no contiguous aligned space.
    // 返すのは生の場所だけ。Tのコンストラクタを呼ぶ仕事はObjectPoolなどの上位へ。
    [[nodiscard]] void* Allocate(std::size_t bytes,
        std::size_t alignment = alignof(std::max_align_t));
    // Allocateが返した先頭アドレスを返す。中腹のポインタや二重返却はfalse。
    [[nodiscard]] bool Free(void* pointer);

    // Never invalidates live allocations. Does not return the arena to the OS.
    // 生存中のデータを吹き飛ばすボタンではない。貸出ゼロのときだけ帳簿を初期化。
    [[nodiscard]] bool TryReleaseAll();
    Stats GetStats() const;
    // 空きの合計ではなく、一度に貸せる連続領域の最大値。断片化すると両者は違う。
    std::size_t GetLargestAvailableBlock(
        std::size_t alignment = alignof(std::max_align_t)) const;

    // Returns zero for invalid requests; uses integer arithmetic only.
    // 全サイズを4 KiBへ切り上げ。1回の丸めの余りは0～4095 B。断片化や管理表は別。
    static std::size_t RoundAllocationSize(std::size_t bytes) noexcept;

private:
    // aligned newには同じアラインメントのaligned deleteを組み合わせる。出口もセット。
    struct StorageDeleter
    {
        void operator()(std::byte* pointer) const noexcept;
    };
    // 4 KiBごとの帳簿。先頭だけ長さを持ち、続きのページには特別な印を付ける。
    struct Page
    {
        // 0 = free; SIZE_MAX = continuation; otherwise allocation length.
        std::size_t allocationPages = 0;
        std::size_t requestedBytes = 0;
    };

    static bool ValidAlignment(std::size_t alignment) noexcept;

    // constの統計取得も帳簿を読むのでロックする。返したデータの中身までは保護しない。
    mutable std::mutex mutex_;
    std::unique_ptr<std::byte, StorageDeleter> storage_;
    // 帳簿自体は通常のSTLヒープ。管理領域に含まれるのは貸出用のstorageだけ。
    std::vector<Page> pages_;
    Stats stats_;
};
