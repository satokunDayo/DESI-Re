// 貸したものが生きている間は消えない、という約束の実演。実ゲームの敵AIではない。
#include "MemorySystemDemo.h"
#include "ResourceManager.h"
#include "MemorySystem/Debug/MemoryProfiler.h"
#include <cstdint>
#include <stdexcept>

namespace
{
    // 普通より厳しい64バイト境界の型でも、ObjectPoolがalignof(T)を維持する例。
    struct alignas(64) EnemyData
    {
        int hp;
        std::byte padding[64 - sizeof(int)]{};
        explicit EnemyData(int initialHp) noexcept : hp(initialHp) {}
    };
    // 条件が崩れたら黙って先へ進まず例外にする、デモ用の確認関数。
    void Require(bool condition)
    {
        if (!condition) throw std::runtime_error("Memory system demonstration failed.");
    }
}

// Systemは継続、Sceneは交換、という寿命の違いを1本の流れで確認する。
void RunMemorySystemDemo(MemoryManager& memory, ResourceManager& resources)
{
    constexpr auto MiB = BlockAllocator::MiB;
    // 6 MiB+1バイトの注文は6 MiB+4 KiB予約になる。65536は住所の境界指定。
    auto persistent = memory.Allocate(ArenaType::System, 6 * MiB + 1, 65536);
    auto temporary = memory.Allocate(ArenaType::Scene, 100, 256);
    Require(persistent && temporary);
    Require(reinterpret_cast<std::uintptr_t>(persistent.Data()) % 65536 == 0);
    *static_cast<unsigned char*>(persistent.Data()) = 123;

    // 16席のプールを作り、1体をその場で構築。Rent(100)の100は初期HP。
    auto enemies = memory.CreatePool<EnemyData>(ArenaType::Scene, 16);
    auto enemy = enemies.Rent(100);
    Require(enemy && enemy->hp == 100 && enemies.GetActiveCount() == 1);
    // 同じファイルを2回取得しても実体は共有。同じHandle先になることを確認。
    auto first = resources.AcquireBinary("Test.txt");
    auto second = resources.AcquireBinary("Test.txt");
    Require(first == second);
    MemoryProfiler::PrintDebugReport(memory);

    // まだ借り物があるので交換は失敗するはず。その間も敵やファイルの中身は有効。
    resources.BeginSceneClose();
    Require(!memory.TryResetScene()); // Live data remains valid while busy.
    Require(enemy->hp == 100 && !first->GetBytes().empty());
    // 外側の共有所有とPoolの貸出を全部返す。ここまで済んで初めてSceneを交換できる。
    first.reset();
    second.reset();
    enemy.Reset();
    temporary.Reset();
    Require(memory.TryResetScene()); // Idle pool slabs are returned automatically.
    Require(!enemies.Rent(50));      // An old pool cannot allocate in a new scene.
    Require(*static_cast<unsigned char*>(persistent.Data()) == 123);
    // SystemのデータはScene交換を生き延びた。最後に自分で返して残件を確認。
    persistent.Reset();
    Require(!MemoryProfiler::HasOutstandingAllocations(memory));
    MemoryProfiler::PrintDebugReport(memory);

    // Leave one unused cache entry to demonstrate delayed collection in Run().
    // このローカルHandleは帰り際に消え、キャッシュだけが残る。EngineのUpdateで遅延回収される例。
    auto cached = resources.AcquireBinary("Test.txt");
    Require(cached != nullptr);
}
