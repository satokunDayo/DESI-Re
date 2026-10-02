#pragma once
// 以前のAPIを残すための薄い窓口。現在のEngineのメイン経路はMemoryManagerを使う。
#include "MemorySystem/Core/BlockAllocator.h"

// Compatibility facade: existing ResourceManager code can keep its API.
class CpuAssetHeap final
{
public:
    static constexpr std::size_t KiB = BlockAllocator::KiB;
    static constexpr std::size_t MiB = BlockAllocator::MiB;
    static constexpr std::size_t MinBlockSize = BlockAllocator::PageSize;
    static constexpr std::size_t MaxBlockSize = BlockAllocator::MaxAllocationSize;

    // このクラスを実際に生成したときだけ独自の領域を確保する。存在するだけで256 MiBは増えない。
    explicit CpuAssetHeap(std::size_t capacity = 256 * MiB);
    ~CpuAssetHeap() = default;
    CpuAssetHeap(const CpuAssetHeap&) = delete;
    CpuAssetHeap& operator=(const CpuAssetHeap&) = delete;

    // 生ポインタを貸す旧API。MemoryBlockの自動返却がないので、呼ぶ側がFreeと寿命を管理する。
    [[nodiscard]] void* Allocate(std::size_t bytes,
        std::size_t alignment = alignof(std::max_align_t));
    [[nodiscard]] bool Free(void* pointer);
    std::size_t GetCapacity() const;
    // 予約量を返す。要求量やプロセス全体のRAM量とは違う。
    std::size_t GetUsedBytes() const;
    std::size_t GetFreeBytes() const;
    std::size_t GetLargestAvailableBlock() const;

private:
    // 管理ロジックをもう1個作らず、BlockAllocatorへ委譲している。
    BlockAllocator allocator_;
};
