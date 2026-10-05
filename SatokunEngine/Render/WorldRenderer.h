#pragma once
// Worldが作ったRenderItemをGPU命令へ変換する。オブジェクトの生成や削除はしない。
#include "World/World.h"
#include "Camera/Camera.h"
#include <wrl/client.h>

// MeshがBoxか外部モデルかは知らない。GPUメッシュと材質を受け取って描く。
class WorldRenderer final
{
public:
    // Mesh用シェーダーと、深度あり/なしの2種類の描画設定を作る。
    void Initialize(ID3D12Device* device);
    // Cameraから投影を得て、各物体の形状・行列・材質を渡して描く。
    void Draw(ID3D12GraphicsCommandList* commands,std::span<const RenderItem> items,
        const Camera& camera,float aspect,GpuMemoryManager& memory);
private:
    // GPUへ渡す姿勢の作業領域。現在のRendererは毎フレームFenceを待つため再利用できる。
    GpuBuffer skinBuffer_;
    std::size_t skinBufferBytes_=0;
    // root_はGPUへ渡す定数と画像の配置規約。Shader側と変更をそろえる必要がある。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    // depthPipeline_は奥行き比較、orderedPipeline_は描画順。Camera設定で選ぶ。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPipeline_,orderedPipeline_;
    // 画像なしの材質でも有効なDescriptorをセットするためのnull SRV。白画像を確保してはいない。
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> nullTextureHeap_;
};
