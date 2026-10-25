#pragma once
// GPUの画像と、シェーダーがその画像を見つけるSRVをひとまとめに所有する。
#include "Render/GpuMemory.h"
#include <wrl/client.h>

class D3D12Renderer;
class SpriteRenderer;

// 画素を持つGPUリソース＋シェーダーから参照するためのSRV。
// 初版は1画像につき1個のSRVヒープ。共通ヒープ化は後でこの内部を差し替える。
class GpuTexture final
{
public:
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    // 画素寸法。画面上の表示サイズはSpriteDrawやTransform側で別に決める。
    UINT Width() const noexcept { return width_; }
    UINT Height() const noexcept { return height_; }
    // 画像本体の割当量。SRVヒープなども含めたプロセス全体のGPU使用量ではない。
    std::uint64_t AllocationBytes() const noexcept { return allocation_->Size(); }
private:
    friend class D3D12Renderer;
    friend class SpriteRenderer;
    friend class WorldRenderer;
    // Rendererだけが作れる入口。転送やSRV設定を飛ばした未完成画像を外へ出さない。
    GpuTexture() = default;
    // 画素本体の所有。CPU画像へのポインタを保持しているわけではない。
    std::shared_ptr<GpuAllocation> allocation_;
    // SRVは画像の住所録のようなもの。画像本体とは別に寿命を守る必要がある。
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    UINT width_ = 0;
    UINT height_ = 0;
};
// 読み取り用の共有所有。Scene・Sprite・描画中のRendererが同じ画像を保持できる。
using GpuTextureHandle = std::shared_ptr<const GpuTexture>;
