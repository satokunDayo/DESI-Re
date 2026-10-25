// 4 KiBページの連続した空きを探す実装。移動中のデータを詰め直す機能はまだない。
#include "MemorySystem/Core/BlockAllocator.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
    // 「このページは前のブロックの続き」の印。ここを先頭としてFreeしてはいけない。
    constexpr std::size_t Continuation = (std::numeric_limits<std::size_t>::max)();
}

// 最後に大元の領域をOS側のヒープへ返す。個々のFreeではここまで返さない。
void BlockAllocator::StorageDeleter::operator()(std::byte* pointer) const noexcept
{
    ::operator delete(pointer, std::align_val_t{ PageSize });
}

// 端数ページが出ない容量だけ受け付ける。管理表と本体は別々に確保する。
BlockAllocator::BlockAllocator(std::size_t capacity)
{
    if (capacity == 0 || capacity % PageSize != 0)
    {
        throw std::invalid_argument("Capacity must be a positive multiple of 4 KiB.");
    }
    storage_.reset(static_cast<std::byte*>(
        ::operator new(capacity, std::align_val_t{ PageSize })));
    pages_.resize(capacity / PageSize);
    stats_.capacityBytes = capacity;
}

// 0は不可、2の累乗だけ許可。64や65536はOK、3や100は境界指定にならない。
bool BlockAllocator::ValidAlignment(std::size_t alignment) noexcept
{
    return std::has_single_bit(alignment) && alignment <= MaxAlignment;
}

// 要求ゼロと上限超えは失敗。計算は整数だけなので大きいサイズでも丸め誤差を持ち込まない。
std::size_t BlockAllocator::RoundAllocationSize(std::size_t bytes) noexcept
{
    if (bytes == 0 || bytes > MaxAllocationSize)
    {
        return 0;
    }
    // 4 KiBの席を必要な数だけ借りる。6 MiB+1 Bなら6 MiB+4 KiB。
    return ((bytes - 1) / PageSize + 1) * PageSize;
}

// 最初に見つかった「サイズも境界も満たす空き」を使う。いわゆるfirst-fit方式。
void* BlockAllocator::Allocate(std::size_t bytes, std::size_t alignment)
{
    const std::size_t reserved = RoundAllocationSize(bytes);
    if (reserved == 0 || !ValidAlignment(alignment))
    {
        return nullptr;
    }

    std::lock_guard lock(mutex_);
    std::size_t scan = 0;
    while (scan < pages_.size())
    {
        if (pages_[scan].allocationPages != 0)
        {
            scan += pages_[scan].allocationPages == Continuation
                ? 1 : pages_[scan].allocationPages;
            continue;
        }

        // 空きの先頭を覚え、使用中ページにぶつかるまで連続範囲を数える。
        const std::size_t begin = scan;
        while (scan < pages_.size() && pages_[scan].allocationPages == 0)
        {
            ++scan;
        }
        void* candidate = storage_.get() + begin * PageSize;
        std::size_t space = (scan - begin) * PageSize;

        // Align the actual address, not merely the offset from the arena base.
        // 大事。先頭からの距離ではなく実アドレスをそろえる。元の領域が64 KiB境界とは限らない。
        if (std::align(alignment, reserved, candidate, space) == nullptr)
        {
            continue;
        }
        const auto offset = static_cast<std::size_t>(
            static_cast<std::byte*>(candidate) - storage_.get());
        // 境界合わせで前に残った余白は未使用のまま。他の小さい注文へ貸せる。
        const std::size_t firstPage = offset / PageSize;
        const std::size_t count = reserved / PageSize;

        // Padding before candidate remains free. No header precedes the payload.
        for (std::size_t page = firstPage; page < firstPage + count; ++page)
        {
            pages_[page].allocationPages = Continuation;
        }
        // 先頭に予約ページ数と本来の注文量を記録。Freeするときの返金明細になる。
        pages_[firstPage] = { count, bytes };
        stats_.requestedBytes += bytes;
        stats_.reservedBytes += reserved;
        ++stats_.activeAllocations;
        return candidate;
    }
    return nullptr;
}

// ポインタの所属・ページ境界・先頭の印を確認してから帳簿を戻す。Tの破棄は呼ぶ側の仕事。
bool BlockAllocator::Free(void* pointer)
{
    if (pointer == nullptr)
    {
        return true;
    }
    std::lock_guard lock(mutex_);
    // 別の領域のポインタ同士を引き算しないよう、アドレス値として範囲を調べる。
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto base = reinterpret_cast<std::uintptr_t>(storage_.get());
    if (address < base || address - base >= stats_.capacityBytes)
    {
        return false;
    }
    const std::size_t offset = static_cast<std::size_t>(address - base);
    if (offset % PageSize != 0)
    {
        return false;
    }
    const std::size_t firstPage = offset / PageSize;
    const Page head = pages_[firstPage];
    if (head.allocationPages == 0 || head.allocationPages == Continuation)
    {
        return false;
    }

    // 隣同士が空けば、次のAllocateの走査で一続きの空きになる。結合用の別リストは持たない。
    for (std::size_t page = firstPage; page < firstPage + head.allocationPages; ++page)
    {
        pages_[page] = {};
    }
    stats_.requestedBytes -= head.requestedBytes;
    stats_.reservedBytes -= head.allocationPages * PageSize;
    --stats_.activeAllocations;
    // Adjacent free pages form a single range on the next scan automatically.
    return true;
}

// 誰かが借りたままなら失敗する。使われている住所を消す事故だけは起こさない。
bool BlockAllocator::TryReleaseAll()
{
    std::lock_guard lock(mutex_);
    if (stats_.activeAllocations != 0)
    {
        return false;
    }
    std::fill(pages_.begin(), pages_.end(), Page{});
    stats_.requestedBytes = 0;
    stats_.reservedBytes = 0;
    return true;
}

// ロック中に値をコピーして返す。帳簿への参照を外へ貸さない。
BlockAllocator::Stats BlockAllocator::GetStats() const
{
    std::lock_guard lock(mutex_);
    return stats_;
}

// 境界合わせの余白を引き、実際に注文できるサイズへ切り下げて見積もる。
std::size_t BlockAllocator::GetLargestAvailableBlock(std::size_t alignment) const
{
    if (!ValidAlignment(alignment))
    {
        return 0;
    }
    std::lock_guard lock(mutex_);
    std::size_t largest = 0;
    std::size_t scan = 0;
    while (scan < pages_.size())
    {
        if (pages_[scan].allocationPages != 0)
        {
            scan += pages_[scan].allocationPages == Continuation
                ? 1 : pages_[scan].allocationPages;
            continue;
        }
        const std::size_t begin = scan;
        while (scan < pages_.size() && pages_[scan].allocationPages == 0)
        {
            ++scan;
        }
        void* candidate = storage_.get() + begin * PageSize;
        std::size_t space = (scan - begin) * PageSize;
        if (std::align(alignment, PageSize, candidate, space) != nullptr)
        {
            // std::align subtracts only the alignment padding from space.
            const std::size_t available = (space / PageSize) * PageSize;
            largest = (std::max)(largest, (std::min)(available, MaxAllocationSize));
        }
    }
    return largest;
}
