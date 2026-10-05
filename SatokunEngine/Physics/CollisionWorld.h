#pragma once
// Worldの現在位置を読んで判定する係。メインスレッド専用、GPUは使わない。
#include "World/World.h"

enum class TriggerPhase { Enter, Stay, Exit };
struct TriggerEvent
{
    TriggerPhase phase=TriggerPhase::Enter;
    ObjectId first,second; // 少なくとも片方がTrigger。所有権も生ポインタも保存しない。
};
struct MoveResult
{
    DirectX::XMFLOAT3 requested{},actual{};
    bool blockedX=false,blockedY=false,blockedZ=false;
    bool startedOverlapping=false;
};

class CollisionWorld final
{
public:
    // Worldより先に破棄する。最大同時Triggerペア数は固定で、超過は例外で知らせる。
    CollisionWorld(MemoryManager& memory,World& world,std::size_t maxTriggerPairs=256);
    ~CollisionWorld();
    CollisionWorld(const CollisionWorld&)=delete;
    CollisionWorld& operator=(const CollisionWorld&)=delete;
    // 全員を動かした後に1回呼ぶ。イベントは次のStep/Resetまで有効。
    std::span<const TriggerEvent> Step();
    void Reset() noexcept;
    // 移動役はAABBのSolid。相手はAABB/SphereのSolid。Triggerでは止まらない。
    // X→Y→Zの軸別スイープ。1回の移動量が薄い壁を越えても途中で止める。
    MoveResult MoveAabb(ObjectId mover,const DirectX::XMFLOAT3& delta,float skin=.001f);
    std::size_t GetActiveTriggerPairCount() const noexcept { return pairCount_; }
private:
    struct Body {ObjectId id; WorldCollider shape; ColliderMode mode; std::uint32_t category,mask;};
    struct Pair {ObjectId a,b;bool operator==(const Pair&) const=default;};
    static bool Less(const Pair&,const Pair&) noexcept;
    static bool Allows(const Body&,const Body&) noexcept;
    void Collect();
    World& world_;
    std::size_t capacity_,pairCapacity_,bodyCount_=0,pairCount_=0,eventCount_=0;
    // Collider本体はGameObjectのPool内。作業配列もSceneArenaに置いて寿命をそろえる。
    MemoryBlock bodies_,previous_,next_,events_;
};
