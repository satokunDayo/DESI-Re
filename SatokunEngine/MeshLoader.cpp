// CPUで形状を作る→GPUへ転送→weak登録、という流れ。メインスレッドで使う。
#include "MeshLoader.h"
#include "D3D12Renderer.h"
#include <stdexcept>
// まだファイルパスを受け取る関数ではない。FBX読込済みと誤解しないように。
GpuMeshHandle MeshLoader::LoadPrimitive(PrimitiveShape shape,ArenaType scope)
{
    PruneExpired();
    auto arena=memory_.GetArena(scope);
    // Arenaが閉じる間はロード禁止。キャッシュに当たる場合も同じ規則にする。
    auto operation=arena->BeginOperation();
    if(!operation)throw std::runtime_error("Cannot load a mesh into a closing arena.");
    // 同名Sceneでも再入場すればArena IDが変わるので、旧世代の目録と混ざらない。
    const auto key=std::make_pair(arena->GetId(),shape);
    // 実体が生きているならshared_ptrを取得して返す。なければ作り直す。
    if(auto found=cache_.find(key);found!=cache_.end())if(auto mesh=found->second.lock())return mesh;
    // ここでCPU Arenaへ頂点と番号を確保する。
    auto cpu=CpuMesh::MakePrimitive(memory_,scope,shape);
    // この関数は転送完了まで待つので、LoadPrimitiveを抜けてcpuが消えても大丈夫。
    auto gpu=renderer_.UploadMesh(cpu);
    cache_[key]=gpu;
    return gpu;
}
// 消すのは住所録の抜け殻。ここからGPUリソースを強制的に追い出すことはしない。
void MeshLoader::PruneExpired(){std::erase_if(cache_,[](const auto& entry){return entry.second.expired();});}
