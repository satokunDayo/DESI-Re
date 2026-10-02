// 画像転送の実装。同じクラスのメソッドを用途別cppへ分けてあるだけで、別Rendererではない。
#include "D3D12Renderer.h"
#include "CpuImage.h"
#include <cstring>
#include <stdexcept>

// CPUの連続RGBA画素を、GPUに適した画像リソースへコピーする。
GpuTextureHandle D3D12Renderer::UploadImage(const CpuImage& image)
{
    if (!device_ || !image.width || !image.height || image.width > 16384 || image.height > 16384 ||
        !image.pixels || image.pixels.Size() < std::uint64_t(image.width) * image.height * 4)
        throw std::invalid_argument("Invalid image upload.");
    // この転送も描画と同じAllocator/Queueを使う。前の仕事が終わるまではノートを消せない。
    Flush(); // 同じDirect Queue / CommandAllocatorを再利用する前に待つ。
    // まず返却係を用意する。途中で例外が出ても、既に確保したメンバーは自動で片付く。
    auto texture = std::shared_ptr<GpuTexture>(new GpuTexture());
    texture->width_ = image.width;
    texture->height_ = image.height;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = image.width;
    desc.Height = image.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    texture->allocation_ = gpuMemory_.CreateTexture(desc);

    // シェーダーへ見せる住所録を1画像につき1個作る初版。大量画像の最適化は今後。
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckGpu(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&texture->srvHeap_)));
    // 画像をどう読むかの情報。MipLevels=1なので縮小用ミップ列はまだ作っていない。
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(texture->allocation_->Resource(), &srv,
        texture->srvHeap_->GetCPUDescriptorHandleForHeapStart());

    // DX12に転送用の行間隔と総量を聞く。width×4だけで1行を進めると合わない場合がある。
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 totalBytes = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
    // CPUから書けるUPLOADバッファを中継地点にする。最終的に描画する画像とは別確保。
    auto upload = gpuMemory_.CreateBuffer(totalBytes, D3D12_HEAP_TYPE_UPLOAD,
        GpuCategory::Upload, D3D12_RESOURCE_STATE_GENERIC_READ);
    // CPUはここを読む予定がないと伝える。Mapは画像の転送完了待ちではない。
    const D3D12_RANGE noRead{ 0,0 };
    void* mapped = nullptr;
    CheckGpu(upload->Resource()->Map(0, &noRead, &mapped));
    // CPU側の行幅はwidth×4、転送側はRowPitch。1行ずつコピーして余白の違いを吸収する。
    for (UINT row = 0; row < image.height; ++row)
        std::memcpy(static_cast<std::byte*>(mapped) + footprint.Offset + std::size_t(row) * footprint.Footprint.RowPitch,
            static_cast<const std::byte*>(image.pixels.Data()) + std::size_t(row) * image.width * 4,
            std::size_t(image.width) * 4);
    // CPUが書いた範囲を知らせてUnmap。これだけではGPU画像へのコピーはまだ終わっていない。
    const D3D12_RANGE written{ 0, static_cast<SIZE_T>(totalBytes) };
    upload->Resource()->Unmap(0, &written);

    CheckGpu(allocator_->Reset());
    CheckGpu(commands_->Reset(allocator_.Get(), nullptr));
    // コピー先はTextureの0番サブリソース、元はUPLOADのfootprintで指定する。
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = texture->allocation_->Resource();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = upload->Resource();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    // ここではコピー命令を書いているだけ。Queueへ出すまでGPUは実行しない。
    commands_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    // コピー先として使った後、Pixel Shaderから読める状態へ遷移させる。
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture->allocation_->Resource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commands_->ResourceBarrier(1, &barrier);
    CheckGpu(commands_->Close());

    // Submit後の例外でも、RendererがFenceを待つまで所有権を保持する。
    // 命令を送る前に所有を保持する。送信後の例外でも転送元を即解放しないため。
    inFlightTextures_.push_back(texture);
    inFlightUpload_ = upload;
    ID3D12CommandList* lists[] = { commands_.Get() };
    queue_->ExecuteCommandLists(1, lists);
    // この待機のおかげで、呼び出し元は返ってきた直後にCPU画素を片付けられる。
    WaitForGpu();
    // 戻った時点で転送完了。CPU画素・UPLOADを安全に返せる。
    return texture;
}
