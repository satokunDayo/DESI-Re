// シーン切替でCPU領域を交換する実装。借り物が残ったまま新しいSceneへ使い回さない。
#include "MemorySystem/Manager/MemoryManager.h"
#include <limits>
#include <stdexcept>

// 合計容量の整数あふれを確認し、System/Sceneをそれぞれ作る。容量不足なら例外。
MemoryManager::MemoryManager(std::size_t systemBytes, std::size_t sceneBytes)
    : sceneCapacity_(sceneBytes)
{
    if (systemBytes > (std::numeric_limits<std::size_t>::max)() - sceneBytes)
        throw std::invalid_argument("Combined arena capacity overflows.");
    system_ = MemoryArena::Create(systemBytes);
    scene_ = MemoryArena::Create(sceneBytes);
}

// ロック中に現在のArenaをコピーする。差し替え後でも取得済みの古いArenaの寿命は残る。
std::shared_ptr<MemoryArena> MemoryManager::GetArena(ArenaType type) const
{
    std::lock_guard lock(mutex_);
    return type == ArenaType::System ? system_ : scene_;
}

// どちらへ頼むか決めるだけ。丸め方や境界処理をここに重複させない。
MemoryBlock MemoryManager::Allocate(ArenaType type, std::size_t bytes, std::size_t alignment)
{
    return GetArena(type)->Allocate(bytes, alignment);
}

// 差し替えと同じロックを使い、閉めるArenaと返すIDが食い違わないようにする。
std::uint64_t MemoryManager::BeginSceneClose()
{
    // Serializes with scene replacement, so the intended current arena closes.
    std::lock_guard lock(mutex_);
    scene_->BeginClose();
    return scene_->GetId();
}

// 閉じられたときだけ作り直す。falseは「まだ誰かが使っている」か終了中。
bool MemoryManager::TryResetScene()
{
    std::lock_guard lock(mutex_);
    if (shuttingDown_ || !scene_->TryClose()) return false;
    // Old payload memory has been freed before allocating the new arena.
    // 古い貸出用領域は解放済み。旧Scene用と新Scene用の本体を同時に抱えない。
    scene_ = MemoryArena::Create(sceneCapacity_);
    return true;
}

// これ以降はSceneの再生成も止める。終了したいのに新しい領域が生えると困る。
void MemoryManager::BeginShutdown()
{
    std::lock_guard lock(mutex_);
    shuttingDown_ = true;
    system_->BeginClose();
    scene_->BeginClose();
}

// 片方が忙しくても、もう片方の片付けは試す。両方Closedならtrue。
bool MemoryManager::TryFinishShutdown()
{
    std::lock_guard lock(mutex_);
    shuttingDown_ = true;
    system_->BeginClose();
    scene_->BeginClose();
    const bool systemDone = system_->TryClose();
    const bool sceneDone = scene_->TryClose();
    return systemDone && sceneDone;
}
