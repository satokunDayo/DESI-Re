#pragma once
// Scene内の物置き場。Sceneは進行、Worldはオブジェクトの所有と描画一覧の組み立てを担当する。
#include "GameObject.h"
#include <vector>

// Sceneが所有する空間。メインスレッド専用。GameObjectはSceneArenaのPoolで管理する。
class World final
{
public:
    // 容量分のPoolと描画配列を先に確保。満杯になったら自動拡張せず失敗を知らせる。
    explicit World(MemoryManager& memory,std::size_t capacity=128);
    ~World();
    World(const World&)=delete;
    World& operator=(const World&)=delete;
    // 注文書から1個生成し、後で探せるIDを返す。返り値のポインタを永続保存する方式ではない。
    [[nodiscard]] ObjectId Spawn(const ObjectDesc& desc);
    // 一括生成。途中で失敗したら今回の物だけ返す。既存の削除予約には触れない。
    [[nodiscard]] std::vector<ObjectId> SpawnBatch(std::span<const ObjectDesc> descriptions);
    // 生きているIDなら借りたポインタ、無効ならnullptr。Worldの破棄や実体削除後には使わない。
    GameObject* Find(ObjectId id) noexcept;
    const GameObject* Find(ObjectId id) const noexcept;
    // 削除予約だけ付ける。以後Findと描画からは外れ、実体の返却はCommitChangesで行う。
    bool RequestDestroy(ObjectId id) noexcept;
    // 更新処理の区切りで予約を反映し、Poolへ返す。GetCountが減るのもこの時。
    void CommitChanges() noexcept;
    // 全員を返却する。Worldの容量やPoolの土台はWorld自体が残る間は保持する。
    void Clear() noexcept;
    std::size_t GetCount() const noexcept{return count_;}
    std::size_t GetCapacity() const noexcept{return slots_.size();}
    // 読み取り走査用。空席・削除予約済みなら無効ID。席番号だけをID代わりに保存しない。
    ObjectId GetIdAt(std::size_t index) const noexcept;
    // 次のBuildRenderList/Spawn/RequestDestroy/CommitChanges/Clearまで有効。
    // 描画に必要なデータだけを並べた借り物のspan。保持して次フレームへ持ち越さない。
    std::span<const RenderItem> BuildRenderList() const;
private:
    // 席ごとの名簿。Handleが実体を所有し、世代と削除予約が有効性を決める。
    struct Slot
    {
        ObjectPool<GameObject>::Handle object;
        std::uint64_t generation=1,spawnOrder=0;
        bool pendingDestroy=false;
    };
    const Slot* Locate(ObjectId id) const noexcept;
    void InvalidateRenderList() const noexcept;
    void Release(Slot& slot) noexcept;
    std::uint64_t id_,nextSpawnOrder_=1;
    ObjectPool<GameObject> pool_;
    // ID用の名簿は通常ヒープ。GameObjectの本体とRenderItem配列はSceneArenaに置く。
    std::vector<Slot> slots_; // 索引メタデータは通常ヒープ
    MemoryBlock renderMemory_;
    // constの一覧作成でも内部の一時配列は更新する。constだから並列安全、とはならない。
    mutable std::size_t renderCount_=0;
    std::size_t count_=0;
};
