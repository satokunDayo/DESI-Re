#pragma once
// ファイルをCPUへ読み込み、同じものを共有する係。画像の展開やGPU転送は担当外。
#include "MemorySystem/Manager/MemoryManager.h"
#include <filesystem>
#include <future>
#include <map>
#include <optional>

// ファイルの中身をそのまま持つ箱。PNGもテキストも、この段階では単なるバイト列。
class BinaryAsset final
{
public:
    // spanは所有しない。使う間はBinaryAssetHandleを生かし、読み取りだけ行う。
    std::span<const std::byte> GetBytes() const noexcept
    {
        return {static_cast<const std::byte*>(block_.Data()), block_.Size()};
    }
private:
    friend class ResourceManager;
    explicit BinaryAsset(MemoryBlock block) noexcept : block_(std::move(block)) {}
    MemoryBlock block_;
};
// 参照カウント付きの共有所有。最後の所有が外れるとMemoryBlockもArenaへ戻る。
using BinaryAssetHandle = std::shared_ptr<const BinaryAsset>;

// Thread-safe calls, but manager lifetime must cover all callers.
// AcquireBinary blocks the caller; this class does not create worker threads.
class ResourceManager final
{
public:
    // rootはAssetsの基準位置、unusedDelayは誰も使わなくなってから回収までの猶予。
    ResourceManager(MemoryManager& memory, std::filesystem::path root,
        double unusedDelaySeconds = 3.0);
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // 同じArena・同じ正規化パスなら共有。呼んだスレッドで読み終える同期API。
    [[nodiscard]] BinaryAssetHandle AcquireBinary(const std::filesystem::path& relativePath,
        ArenaType scope = ArenaType::Scene);
    // Engineが毎フレーム呼び、未使用キャッシュの経過時間を進める。距離判定はまだない。
    void Update(double realDeltaSeconds);
    void BeginSceneClose();
    void BeginShutdown();
    std::size_t GetResidentCount() const;
    std::size_t GetPendingCount() const;
    std::size_t GetUsedBytes() const;

private:
    // キャッシュ1件の帳簿。完成済みassetか、読み込み結果を待つpendingを持つ。
    struct Entry
    {
        ArenaType scope = ArenaType::Scene;
        BinaryAssetHandle asset;
        std::shared_future<BinaryAssetHandle> pending;
        std::optional<double> unusedSince;
    };
    // Arena IDもキーに含める。新しいシーンへ古い領域のファイルを渡さないため。
    using Key = std::pair<std::uint64_t, std::filesystem::path>;

    BinaryAssetHandle ReadFile(const std::shared_ptr<MemoryArena>& arena,
        const std::filesystem::path& path);
    // 容量が足りないとき、同じArenaの未使用キャッシュを猶予なしで回収する。
    void CollectUnusedNow(std::uint64_t arenaId);
    // 目録側の所有を外すだけ。ゲーム側のHandleがあれば実体はまだ残る。
    void DropCache(std::optional<ArenaType> scope, std::optional<std::uint64_t> arenaId = std::nullopt);

    // MemoryManagerは借り物なので、ResourceManagerを使う間は先に破棄しない。
    MemoryManager& memory_;
    const std::filesystem::path root_;
    const double unusedDelay_;
    mutable std::mutex mutex_;
    std::map<Key, std::shared_ptr<Entry>> entries_;
    // このManagerの回収用時計。OS時刻ではなくUpdateで渡された経過時間の合計。
    double now_ = 0.0;
};
