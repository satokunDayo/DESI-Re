#pragma once
// 見た目とは独立した当たり判定の注文書。GPUも入力も知らない。
#include "Transform.h"
#include <cstdint>
struct MeshBounds;

enum class ColliderShape { Aabb, Sphere };
enum class ColliderMode { Solid, Trigger };

// Transformを適用し終えた、その瞬間の形。AABBは回転後もワールド軸に平行な箱。
struct WorldCollider
{
    ColliderShape shape = ColliderShape::Aabb;
    DirectX::XMFLOAT3 center{}, halfExtents{};
    float radius = 0;
};

struct Collider
{
    static Collider Box(DirectX::XMFLOAT3 halfExtents, DirectX::XMFLOAT3 center = {},
        ColliderMode mode = ColliderMode::Solid);
    static Collider Sphere(float radius, DirectX::XMFLOAT3 center = {},
        ColliderMode mode = ColliderMode::Solid);
    // MeshのローカルBoundsから作る。頂点やGPU資産を複製するわけではない。
    static Collider AutoFit(ColliderShape shape = ColliderShape::Aabb,
        ColliderMode mode = ColliderMode::Solid);
    void Validate() const;
    WorldCollider ToWorld(const Transform& transform, const MeshBounds* meshBounds = nullptr) const;

    ColliderShape shape = ColliderShape::Aabb;
    ColliderMode mode = ColliderMode::Solid;
    bool enabled = true;
    bool autoFit = false;
    DirectX::XMFLOAT3 center{}, halfExtents{ .5f,.5f,.5f };
    float radius = .5f;
    // 配置・描画レイヤーとは別の衝突フィルター。双方が相手を許可した時だけ判定する。
    std::uint32_t category = 1, mask = UINT32_MAX;
};

namespace Collision
{
    // Triggerは接している状態も重なり扱い。Penetratesは接するだけならfalse。
    bool Overlaps(const WorldCollider& a, const WorldCollider& b);
    bool Penetrates(const WorldCollider& a, const WorldCollider& b);
}
