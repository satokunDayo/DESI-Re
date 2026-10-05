// 3D描画の接続とMesh転送をまとめた実装。形状の作成そのものはCpuMesh側。
#include "Render/D3D12Renderer.h"
#include <cstring>
#include <stdexcept>

// Worldのオブジェクトから描画一覧を作り、GPUが使う資産の寿命をRendererへつなぐ。
void D3D12Renderer::DrawWorld(D3D12_CPU_DESCRIPTOR_HANDLE rtv,const World& world,const Camera& camera)
{
    // 戻り値はWorld内部配列へのspan。Worldの変更や次の一覧作成までの借り物。
    auto items=world.BuildRenderList();
    for(const auto& item:items)
    {
        const auto& mesh=item.renderer.mesh;
        // 形状が別RendererのDevice由来なら拒否。アドレス値があれば何でも描けるわけではない。
        if(!gpuMemory_.Owns(*mesh->vertices_)||!gpuMemory_.Owns(*mesh->indices_))
            throw std::invalid_argument("Mesh belongs to another renderer.");
        // 一覧にあるMeshをGPU完了まで保持する。Textureが付いていればそちらも同様。
        inFlightMeshes_.push_back(mesh);
        const auto& texture=item.renderer.material.texture;
        if(texture)
        {
            if(!gpuMemory_.Owns(*texture->allocation_))throw std::invalid_argument("Texture belongs to another renderer.");
            inFlightTextures_.push_back(texture);
        }
    }
    const auto dsv=internalTarget_->Dsv();
    // 前フレームの奥行きを1で消す。これを忘れると、いないはずの物体に隠される。
    commands_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    commands_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    // Worldは内部画像全体へ描画。窓の縦横比を使うと黒帯のある画面で形がゆがむ。
    const UINT width = internalTarget_->Width(), height = internalTarget_->Height();
    const D3D12_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    const D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    commands_->RSSetViewports(1,&viewport);commands_->RSSetScissorRects(1,&scissor);
    worldRenderer_.Draw(commands_.Get(),items,camera,static_cast<float>(width)/height,gpuMemory_);
}

// CPUの頂点列と番号列を、それぞれGPUのDEFAULTバッファへ運ぶ。
GpuMeshHandle D3D12Renderer::UploadMesh(const CpuMesh& cpu)
{
    const auto vertices=cpu.Vertices();const auto indices=cpu.Indices();
    if(!device_||vertices.empty()||indices.empty())throw std::invalid_argument("Invalid mesh upload.");
    Flush();
    auto mesh=std::shared_ptr<GpuMesh>(new GpuMesh());
    // 境界情報はCPUから値でコピー。元のCpuMeshを消しても必要な情報が残る。
    mesh->requiredSkinJoints_=cpu.RequiredSkinJoints();
    mesh->bounds_=cpu.Bounds();mesh->indexCount_=static_cast<UINT>(indices.size());
    const auto vbBytes=vertices.size_bytes(),ibBytes=indices.size_bytes();
    // DEFAULT BufferはCOMMONで生成。最初のCopyでCOPY_DESTへ暗黙昇格する。
    mesh->vertices_=gpuMemory_.CreateBuffer(vbBytes,D3D12_HEAP_TYPE_DEFAULT,GpuCategory::Mesh,D3D12_RESOURCE_STATE_COMMON);
    mesh->indices_=gpuMemory_.CreateBuffer(ibBytes,D3D12_HEAP_TYPE_DEFAULT,GpuCategory::Mesh,D3D12_RESOURCE_STATE_COMMON);
    // GPUアドレス・総バイト数・1頂点の幅を記録。インデックスは32ビット整数として読む。
    mesh->vertexView_={mesh->vertices_->Resource()->GetGPUVirtualAddress(),static_cast<UINT>(vbBytes),sizeof(MeshVertex)};
    mesh->indexView_={mesh->indices_->Resource()->GetGPUVirtualAddress(),static_cast<UINT>(ibBytes),DXGI_FORMAT_R32_UINT};
    // 中継バッファは1本にまとめ、前半へ頂点、後半へインデックスを置く。
    auto upload=gpuMemory_.CreateBuffer(vbBytes+ibBytes,D3D12_HEAP_TYPE_UPLOAD,GpuCategory::Upload,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped=nullptr;const D3D12_RANGE noRead{0,0};
    CheckGpu(upload->Resource()->Map(0,&noRead,&mapped));
    std::memcpy(mapped,vertices.data(),vbBytes);
    // インデックスの開始位置は頂点バイト数の直後。GPUコピー側も同じオフセットを使う。
    std::memcpy(static_cast<std::byte*>(mapped)+vbBytes,indices.data(),ibBytes);
    const D3D12_RANGE written{0,vbBytes+ibBytes};upload->Resource()->Unmap(0,&written);
    CheckGpu(allocator_->Reset());CheckGpu(commands_->Reset(allocator_.Get(),nullptr));
    commands_->CopyBufferRegion(mesh->vertices_->Resource(),0,upload->Resource(),0,vbBytes);
    commands_->CopyBufferRegion(mesh->indices_->Resource(),0,upload->Resource(),vbBytes,ibBytes);
    // コピー完了後の用途をそれぞれ指定。頂点用とインデックス用では状態が違う。
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for(auto& barrier:barriers){barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;}
    barriers[0].Transition.pResource=mesh->vertices_->Resource();barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[1].Transition.pResource=mesh->indices_->Resource();barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_INDEX_BUFFER;
    commands_->ResourceBarrier(2,barriers);CheckGpu(commands_->Close());
    // GPUへ送る前にMeshと転送元を保持。待ち終えた後は返したGpuMeshHandleが所有する。
    inFlightMeshes_.push_back(mesh);inFlightUpload_=upload;
    ID3D12CommandList* lists[]={commands_.Get()};queue_->ExecuteCommandLists(1,lists);WaitForGpu();
    return mesh;
}
