#pragma once
// GPUに転送済みの形状。頂点の並びと、その頂点を結ぶ番号の並びを持つ。
#include "GpuMemory.h"
#include "CpuMesh.h"

class D3D12Renderer;
class WorldRenderer;
class GpuMesh final
{
public:
    GpuMesh(const GpuMesh&)=delete;
    GpuMesh& operator=(const GpuMesh&)=delete;
    // ローカル座標での最小/最大。Transform適用後の当たり判定が自動で付くわけではない。
    const MeshBounds& Bounds() const noexcept{return bounds_;}
    // DrawIndexedInstancedに渡す番号の個数。三角形数は通常この値の1/3。
    UINT IndexCount() const noexcept{return indexCount_;}
    // 頂点とインデックスの実割当量の合計。CPU側の元データは含まない。
    std::uint64_t AllocationBytes() const noexcept{return vertices_->Size()+indices_->Size();}
private:
    friend class D3D12Renderer;
    friend class WorldRenderer;
    // 転送とビュー設定までRendererが仕上げる。外から不完全なMeshを作らせない。
    GpuMesh()=default;
    // DEFAULTヒープ上の2本のバッファを共有所有する。描画中はRendererも所有を足す。
    GpuBuffer vertices_,indices_;
    // ViewはGPUアドレス・長さ・1頂点の幅を記した値。View自体はバッファを所有しない。
    D3D12_VERTEX_BUFFER_VIEW vertexView_{};
    D3D12_INDEX_BUFFER_VIEW indexView_{};
    UINT indexCount_=0;
    MeshBounds bounds_;
};
// 複数オブジェクトが同じ形状を共有できる。位置や色は各GameObject側で変える。
using GpuMeshHandle=std::shared_ptr<const GpuMesh>;
