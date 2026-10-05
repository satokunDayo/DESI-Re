#pragma once
// SystemとSceneの2つのArenaを束ねる窓口。Engineが所有する普通のクラスで、シングルトンではない。
#include "MemorySystem/Core/MemoryArena.h"
#include "MemorySystem/Core/ObjectPool.h"

// Systemはシーンをまたぐデータ、Sceneは切替時に返したいデータ、という寿命の区分。
enum class ArenaType { System, Scene };

class MemoryManager final
{
public:
    // 既定は64+192=256 MiB。2つの容量を自動で融通し合う仕組みはまだない。
    explicit MemoryManager(std::size_t systemBytes = 64 * BlockAllocator::MiB,
        std::size_t sceneBytes = 192 * BlockAllocator::MiB);
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    // Arenaを共有所有で渡す。戻り値を持つだけでは貸出中扱いにはならない。
    std::shared_ptr<MemoryArena> GetArena(ArenaType type) const;
    // 領域の選択を引き受け、サイズと境界はそのままArenaへ渡す。
    [[nodiscard]] MemoryBlock Allocate(ArenaType type, std::size_t bytes,
        std::size_t alignment = alignof(std::max_align_t));
    // Tごとのプールを作る。名前文字列で型を当てる方式ではなく、コンパイル時に型が決まる。
    template<class T>
    ObjectPool<T> CreatePool(ArenaType type, std::size_t capacity)
    {
        return ObjectPool<T>(GetArena(type), capacity);
    }

    // 閉じ始めたArenaのIDを返す。ResourceManagerはこのIDのキャッシュだけ外せる。
    std::uint64_t BeginSceneClose();
    // Closes the old scene. On success, creates an empty arena with a new ID.
    // Busy means old handles/jobs still exist. They remain valid.
    // 古い借り物が全部戻れば、新しいIDの空Arenaへ交換。Systemの中身は引き継ぐ。
    [[nodiscard]] bool TryResetScene();
    // 両方の受付を停止する。TryFinishShutdownで実際の返却完了を確認する。
    void BeginShutdown();
    [[nodiscard]] bool TryFinishShutdown();

private:
    // Arenaの差し替えを保護するロック。個々のページの帳簿はBlockAllocatorの担当。
    mutable std::mutex mutex_;
    std::shared_ptr<MemoryArena> system_;
    std::shared_ptr<MemoryArena> scene_;
    const std::size_t sceneCapacity_;
    bool shuttingDown_ = false;
};
