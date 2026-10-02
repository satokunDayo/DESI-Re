#pragma once
// 組み込み形状を共有してGPUへ渡す窓口。将来の外部モデル読込もこの周辺へ足す。
#include "GpuMesh.h"
#include <map>
class D3D12Renderer;
// 組み込み形状を同一Arena世代内で共有。外部モデルの読み込みは今後ここへ追加する。
class MeshLoader final
{
public:
    // CPUの確保先とGPU転送先を借りる。両方ともLoaderを使う間は生かしておく。
    MeshLoader(MemoryManager& memory,D3D12Renderer& renderer):memory_(memory),renderer_(renderer){}
    // 同じArena世代で同じ形なら共有。毎回Boxの頂点をVRAMへ重複確保しない。
    GpuMeshHandle LoadPrimitive(PrimitiveShape shape,ArenaType scope=ArenaType::Scene);
    // 実体のなくなったweak登録を掃除するだけ。
    void PruneExpired();
private:
    MemoryManager& memory_;
    D3D12Renderer& renderer_;
    // weakの目録なので、最後のGameObjectや描画中所有が外れればMeshを解放できる。
    std::map<std::pair<std::uint64_t,PrimitiveShape>,std::weak_ptr<const GpuMesh>> cache_;
};
