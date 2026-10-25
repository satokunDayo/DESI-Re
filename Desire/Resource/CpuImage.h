#pragma once
// CPUで触れる展開済み画像。PNGファイルそのものでも、GPUテクスチャでもない。
#include "Resource/ResourceManager.h"

// 元ファイルとは別に、RGBA8へ展開した画素をCPU Arenaで所有する。
struct CpuImage
{
    // ピクセル単位の寸法。RGBA8なので展開後は幅×高さ×4バイト必要。
    unsigned int width = 0;
    unsigned int height = 0;
    // 画素の持ち主。CpuImageが消えるとArenaへ返る。GPU転送後まで永住させなくてよい。
    MemoryBlock pixels;
    // 元ファイルを借り、指定Arenaへ画素を展開して返す。元ファイルの所有は引き取らない。
    static CpuImage Decode(const BinaryAsset& source, MemoryManager& memory, ArenaType scope);
};
