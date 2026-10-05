#pragma once
// CPU上の頂点と番号を所有する型。形状を用意してGPU転送へ渡すまでの入れ物。
#include "MemorySystem/Manager/MemoryManager.h"
#include <DirectXMath.h>
#include <array>

// 位置・法線・UVと、最大4本のボーン番号/重み。WorldRendererの入力表と対応。
struct MeshVertex
{
    DirectX::XMFLOAT3 position;
    // 面の向き。光との角度から明るさを決めるために使う。
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
    // 箱・板は全部0。FBXは上位4本を選び、合計1へ正規化する。
    std::array<std::uint16_t,4> joints{};
    std::array<float,4> weights{};
};
// ローカル空間で全頂点を囲む最小/最大座標。自動Colliderや衝突応答は別途必要。
struct MeshBounds
{
    DirectX::XMFLOAT3 min{}, max{};
};
// 組み込み形状。FBXの入口はModelLoaderで、最後は同じCpuMeshへ通る。
enum class PrimitiveShape { Box, Quad };

// 頂点/インデックスをCPU Arenaで所有。任意の三角形メッシュも同じ入口で渡せる。
class CpuMesh final
{
public:
    // 渡された配列を検査してArenaへコピー。呼ぶ側の配列が先に消えても形状は残る。
    static CpuMesh Create(MemoryManager& memory, ArenaType scope,
        std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices);
    // 箱/板の配列を内部で作り、同じCreateへ通す。外部モデルを追加しても出口を共通にできる。
    static CpuMesh MakePrimitive(MemoryManager& memory, ArenaType scope, PrimitiveShape shape);
    // 配列は読み取りで借りる。CpuMeshの寿命を超えてspanを保持しない。
    std::span<const MeshVertex> Vertices() const noexcept;
    std::span<const std::uint32_t> Indices() const noexcept;
    const MeshBounds& Bounds() const noexcept { return bounds_; }
    std::size_t RequiredSkinJoints() const noexcept { return requiredSkinJoints_; }
private:
    // 2つの領域の持ち主。GPU転送完了後にCpuMeshが消えればCPU領域も返る。
    MemoryBlock vertices_, indices_;
    std::size_t vertexCount_ = 0, indexCount_ = 0;
    MeshBounds bounds_;
    std::size_t requiredSkinJoints_ = 0;
};
