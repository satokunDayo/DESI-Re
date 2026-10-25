#pragma once
// メモリの帳簿を人間向けの文へ変換する係。確保そのものには手を出さない。
#include "MemorySystem/Manager/MemoryManager.h"
#include <string>

class MemoryProfiler final
{
public:
    // 文字列を作るだけなので、必要ならゲーム内表示へ渡すこともできる。
    static std::string MakeReport(const MemoryManager& memory);
    // Visual Studioの出力窓へ送る。ゲーム画面に勝手に文字が描かれるわけではない。
    static void PrintDebugReport(const MemoryManager& memory);
    // Outstanding means still owned, not automatically a leak.
    // 残っている所有や作業を調べる。所有者が生存中なら正常なので、即メモリリークとは呼ばない。
    static bool HasOutstandingAllocations(const MemoryManager& memory);
};
