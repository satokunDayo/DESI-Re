// 注文量・予約量・容量を分けて表示する。数字を混ぜると「どこへ消えた？」になりやすい。
#include "MemorySystem/Debug/MemoryProfiler.h"
#include <Windows.h>
#include <iomanip>
#include <sstream>

namespace
{
    // 列挙値を表示用の名前にする小道具。このcppの中だけで使う。
    const char* Name(ArenaState state)
    {
        switch (state)
        {
        case ArenaState::Open: return "Open";
        case ArenaState::Closing: return "Closing";
        default: return "Closed";
        }
    }
}

// 2つのArenaのスナップショットを取得。エンジン外のSTLやOSの確保は含まない。
std::string MemoryProfiler::MakeReport(const MemoryManager& memory)
{
    const auto system = memory.GetArena(ArenaType::System)->GetSnapshot();
    const auto scene = memory.GetArena(ArenaType::Scene)->GetSnapshot();
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    const auto append = [&](const char* name, const MemoryArena::Snapshot& arena)
    {
        const auto& stats = arena.blocks;
        // 1 MiB=1024×1024バイト。表示時だけ小数へ変換し、確保の計算は整数のまま。
        const double unit = static_cast<double>(BlockAllocator::MiB);
        out << name << " #" << arena.id << " [" << Name(arena.state) << "] "
            << "requested=" << stats.requestedBytes / unit << " MiB, "
            << "reserved=" << stats.reservedBytes / unit << " MiB, "
            << "capacity=" << stats.capacityBytes / unit << " MiB, "
            << "rounding=" << (stats.reservedBytes - stats.requestedBytes) / unit << " MiB, "
            << "blocks=" << stats.activeAllocations << ", operations=" << arena.inFlightOperations << '\n';
    };
    append("System", system);
    append("Scene", scene);
    return out.str();
}

// MakeReportと出力先を分離してあるので、帳簿の作り方を変えず表示先だけ差し替えられる。
void MemoryProfiler::PrintDebugReport(const MemoryManager& memory)
{
    const auto report = MakeReport(memory);
    OutputDebugStringA(report.c_str());
}

// Blockだけでなく作業札も数える。バッファ未確保のロード中を見落とさない。
bool MemoryProfiler::HasOutstandingAllocations(const MemoryManager& memory)
{
    const auto system = memory.GetArena(ArenaType::System)->GetSnapshot();
    const auto scene = memory.GetArena(ArenaType::Scene)->GetSnapshot();
    return system.blocks.activeAllocations != 0 || scene.blocks.activeAllocations != 0
        || system.inFlightOperations != 0 || scene.inFlightOperations != 0;
}
