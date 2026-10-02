#pragma once
// 小さい画面の一式。色・奥行き・GPUへ渡す窓口を一緒に所有する。
#include "GpuMemory.h"
#include <wrl/client.h>

class InternalRenderTarget final
{
public:
    // 全部作れたときだけ完成。途中で失敗しても確保済みの資源はメンバーが返す。
    InternalRenderTarget(ID3D12Device* device, GpuMemoryManager& memory, UINT width, UINT height);
    InternalRenderTarget(const InternalRenderTarget&) = delete;
    InternalRenderTarget& operator=(const InternalRenderTarget&) = delete;

    UINT Width() const noexcept { return width_; }
    UINT Height() const noexcept { return height_; }
    ID3D12Resource* Color() const noexcept { return color_->Resource(); }
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv() const noexcept { return rtv_->GetCPUDescriptorHandleForHeapStart(); }
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const noexcept { return dsv_->GetCPUDescriptorHandleForHeapStart(); }
    ID3D12DescriptorHeap* SrvHeap() const noexcept { return srv_.Get(); }

private:
    UINT width_, height_;
    std::shared_ptr<GpuAllocation> color_, depth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_, dsv_, srv_;
};
