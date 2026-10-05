#include "PostEffect/InternalRenderTarget.h"
#include <stdexcept>

InternalRenderTarget::InternalRenderTarget(ID3D12Device* device, GpuMemoryManager& memory, UINT width, UINT height)
    : width_(width), height_(height)
{
    if (!device) throw std::invalid_argument("Internal render target needs a device.");
    color_ = memory.CreateColorTarget(width, height);
    depth_ = memory.CreateDepth(width, height);

    // RTVは描く窓口、DSVは深度の窓口、SRVは完成した画像を読む窓口。
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = 1;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    CheckGpu(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_)));
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    CheckGpu(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&dsv_)));
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckGpu(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srv_)));

    device->CreateRenderTargetView(color_->Resource(), nullptr, Rtv());
    device->CreateDepthStencilView(depth_->Resource(), nullptr, Dsv());
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(color_->Resource(), &view, srv_->GetCPUDescriptorHandleForHeapStart());
}
