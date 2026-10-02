// 旧名の呼び出しをBlockAllocatorへつなぐだけの実装。新しいコードはArena経由を使う。
#include "CpuAssetHeap.h"

// 渡された容量で内部Allocatorを作る。自動でSystem/Sceneへ振り分ける機能はない。
CpuAssetHeap::CpuAssetHeap(std::size_t capacity)
    : allocator_(capacity)
{
}

// サイズとアラインメントを変更せず転送。失敗ならnullptrがそのまま返る。
void* CpuAssetHeap::Allocate(std::size_t bytes, std::size_t alignment)
{
    return allocator_.Allocate(bytes, alignment);
}

// ここでもTのデストラクタは呼ばない。領域の返却だけ。
bool CpuAssetHeap::Free(void* pointer)
{
    return allocator_.Free(pointer);
}

// 最初に確保した領域全体のサイズ。
std::size_t CpuAssetHeap::GetCapacity() const
{
    return allocator_.GetStats().capacityBytes;
}

// 丸めた後の予約バイト数。
std::size_t CpuAssetHeap::GetUsedBytes() const
{
    return allocator_.GetStats().reservedBytes;
}

// 空きの合計。全部つながっているとは限らない。
std::size_t CpuAssetHeap::GetFreeBytes() const
{
    const auto stats = allocator_.GetStats();
    return stats.capacityBytes - stats.reservedBytes;
}

// 1回で貸せるサイズを知りたいときはこちら。
std::size_t CpuAssetHeap::GetLargestAvailableBlock() const
{
    return allocator_.GetLargestAvailableBlock();
}
