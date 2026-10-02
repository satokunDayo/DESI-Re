// D3D12MAへ確保を頼み、分類ごとの帳簿を付ける。D3D12MA本体のコードはThirdPartyにある。
#include "GpuMemory.h"
#include "ThirdParty/D3D12MA/D3D12MemAlloc.h"
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <limits>

// Managerと各Allocationが共有する裏方。全Allocationが返るまでAllocatorを生かす。
struct GpuMemoryState
{
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<D3D12MA::Allocator> allocator;
    GpuMemoryStats stats;
    UINT frameIndex = 0;
    std::uint64_t externalRTBytes = 0;
    std::size_t externalRTCount = 0;
};

// 失敗したHRESULTを例外にする。表示した16進コードは原因調査の手掛かり。
void CheckGpu(HRESULT result)
{
    if (FAILED(result))
    {
        char message[96]{};
        sprintf_s(message, "DESIRe GPU call failed: HRESULT 0x%08X", static_cast<unsigned>(result));
        throw std::runtime_error(message);
    }
}

namespace
{
    // 列挙値を配列の添字へ。Countや不正な値で配列の外を読ませない。
    std::size_t Index(GpuCategory category)
    {
        const auto i = static_cast<std::size_t>(category);
        if (i >= 4) throw std::invalid_argument("Invalid GPU category.");
        return i;
    }
}

// この時点ではまだGPU領域を持たない。Create成功後にallocation_が入る。
GpuAllocation::GpuAllocation(std::shared_ptr<GpuMemoryState> state, GpuCategory category)
    : state_(std::move(state)), category_(category) {}

// GPUが使用を終えた後に呼ばれる前提。参照カウントだけではGPUの進捗は分からない。
GpuAllocation::~GpuAllocation()
{
    if (!allocation_) return;
    const auto i = static_cast<std::size_t>(category_);
    state_->stats.allocationBytes[i] -= allocation_->GetSize();
    --state_->stats.allocationCount[i];
    // AllocationがResourceも所有する。Resourceに別のReleaseは不要。
    allocation_->Release();
}

// Resourceの所有はD3D12MAのAllocation側。ここで追加のCOM参照を作ってはいない。
ID3D12Resource* GpuAllocation::Resource() const noexcept { return allocation_->GetResource(); }
// 要求サイズではなくD3D12MAが報告する割当サイズ。予算もこの値で数える。
std::uint64_t GpuAllocation::Size() const noexcept { return allocation_->GetSize(); }

// 同じDeviceのAdapterを受け取り、CPU側管理オブジェクトを作る。
void GpuMemoryManager::Initialize(ID3D12Device* device, IDXGIAdapter* adapter)
{
    if (state_ || !device || !adapter) throw std::logic_error("Invalid GPU memory initialization.");
    auto next = std::make_shared<GpuMemoryState>();
    next->device = device;
    D3D12MA::ALLOCATOR_DESC desc{};
    desc.pDevice = device;
    desc.pAdapter = adapter;
    // 4 MiBは希望のブロックサイズ。必ず4 MiB単位だけで確保されるという保証ではない。
    desc.PreferredBlockSize = 4 * GpuMiB; // 小規模エンジン用。実際の確保量は統計で確認。
    CheckGpu(D3D12MA::CreateAllocator(&desc, &next->allocator));
    // Texture 32、Mesh 16、RT 16、Upload 16 MiB。4つ分のVRAMを先に予約する指定ではない。
    next->stats.targetBytes = { 32 * GpuMiB, 16 * GpuMiB, 16 * GpuMiB, 16 * GpuMiB };
    state_ = std::move(next);
}

// 共通の確保処理。実割当サイズが分かってから予算を確認し、超えたら直ちに返す。
std::shared_ptr<GpuAllocation> GpuMemoryManager::Create(const D3D12_RESOURCE_DESC& desc,
    D3D12_HEAP_TYPE heap, GpuCategory category, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clear)
{
    if (!state_) throw std::logic_error("GPU memory is not initialized.");
    const auto i = Index(category);
    D3D12MA::ALLOCATION_DESC request{};
    request.HeapType = heap;
    auto result = std::shared_ptr<GpuAllocation>(new GpuAllocation(state_, category));
    D3D12MA::Allocation* raw = nullptr;
    // ppvResource=nullptr: AllocationだけがResourceのCOM参照を所有する。
    CheckGpu(state_->allocator->CreateResource(&request, &desc, initialState, clear,
        &raw, __uuidof(ID3D12Resource), nullptr));
    const auto bytes = raw->GetSize();
    const auto used = state_->stats.allocationBytes[i];
    const auto target = state_->stats.targetBytes[i];
    // RTは画面リサイズを止めないため上限拒否の対象外。それ以外は未使用回収せず例外で知らせる。
    if (category != GpuCategory::RenderTarget && (used > target || bytes > target - used))
    {
        raw->Release();
        throw std::runtime_error("GPU category budget exceeded. Release unused assets or raise target.");
    }
    // 成功したAllocationを返却係へ引き渡してから帳簿を増やす。ここからRAIIで管理される。
    result->allocation_ = raw;
    state_->stats.allocationBytes[i] += bytes;
    ++state_->stats.allocationCount[i];
    return result;
}

// 頂点・番号・転送用の一次元バイト列。Textureと違って行幅やミップは持たない。
GpuBuffer GpuMemoryManager::CreateBuffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap,
    GpuCategory category, D3D12_RESOURCE_STATES initialState)
{
    if (!bytes || (heap != D3D12_HEAP_TYPE_DEFAULT && heap != D3D12_HEAP_TYPE_UPLOAD))
        throw std::invalid_argument("Invalid GPU buffer request.");
    if ((heap == D3D12_HEAP_TYPE_UPLOAD && category != GpuCategory::Upload) ||
        (heap == D3D12_HEAP_TYPE_DEFAULT && category != GpuCategory::Mesh))
        throw std::invalid_argument("Buffer category must match its heap use.");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return Create(desc, heap, category, initialState);
}

// 転送先としてCOPY_DESTで生まれる。描画前にシェーダー読み取り用へ状態を変える。
std::shared_ptr<GpuAllocation> GpuMemoryManager::CreateTexture(const D3D12_RESOURCE_DESC& desc)
{
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        desc.DepthOrArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1 || desc.Flags != 0)
        throw std::invalid_argument("This loader accepts one RGBA8 texture with one mip.");
    return Create(desc, D3D12_HEAP_TYPE_DEFAULT, GpuCategory::Texture, D3D12_RESOURCE_STATE_COPY_DEST);
}

// D3D12MAへフレーム番号を知らせる。これ自体はGPU完了待ちやLRU回収ではない。
void GpuMemoryManager::BeginFrame()
{
    if (state_) state_->allocator->SetCurrentFrameIndex(state_->frameIndex++);
}

// 自前のカテゴリ帳簿に、バックバッファ分とD3D12MA/OS側の統計を添えて返す。
GpuMemoryStats GpuMemoryManager::GetStats() const
{
    if (!state_) return {};
    auto stats = state_->stats;
    stats.allocationBytes[2] += state_->externalRTBytes;
    stats.allocationCount[2] += state_->externalRTCount;
    // local/nonLocalはメモリ区分。OS budgetは変動し得るので、GPUの公称VRAM容量とは別物。
    D3D12MA::Budget local{}, nonLocal{};
    state_->allocator->GetBudget(&local, &nonLocal);
    stats.allocatorBlockBytes = local.Stats.BlockBytes + nonLocal.Stats.BlockBytes;
    stats.localUsageBytes = local.UsageBytes;
    stats.localBudgetBytes = local.BudgetBytes;
    stats.nonLocalUsageBytes = nonLocal.UsageBytes;
    stats.nonLocalBudgetBytes = nonLocal.BudgetBytes;
    return stats;
}

// 新しい目標値を記録するだけ。下げた時点で使用量が超えていても既存データは残る。
void GpuMemoryManager::SetTarget(GpuCategory category, std::uint64_t bytes)
{
    if (!state_) throw std::logic_error("GPU memory is not initialized.");
    state_->stats.targetBytes[Index(category)] = bytes;
}

// 外部所有分を置き換える。前の画面サイズの量へ足し続けない。
void GpuMemoryManager::SetExternalRenderTargets(std::uint64_t bytes, std::size_t count)
{
    if (!state_) throw std::logic_error("GPU memory is not initialized.");
    // SwapChain所有分は計上だけ。RT目標は監視用で、上限拒否も専用の警告表示もまだない。
    state_->externalRTBytes = bytes;
    state_->externalRTCount = count;
}

// 共有Stateの一致で管理元を確認。同じ形のデータでも別Deviceなら使い回せない。
bool GpuMemoryManager::Owns(const GpuAllocation& allocation) const noexcept
{
    return state_ && allocation.state_ == state_;
}

// 深度はD32_FLOAT。1が奥なので、毎フレーム1で消して手前の値を書き込む。
std::shared_ptr<GpuAllocation> GpuMemoryManager::CreateDepth(UINT width, UINT height)
{
    if (!width || !height || width > 16384 || height > 16384) throw std::invalid_argument("Invalid depth size.");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT; desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.DepthStencil.Depth = 1;
    return Create(desc, D3D12_HEAP_TYPE_DEFAULT, GpuCategory::RenderTarget, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear);
}

// 書き込み可能な内部画像。最初もフレーム終わりも、シェーダーで読める状態にそろえる。
std::shared_ptr<GpuAllocation> GpuMemoryManager::CreateColorTarget(UINT width, UINT height)
{
    if (!width || !height || width > 16384 || height > 16384)
        throw std::invalid_argument("Invalid internal render target size.");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    // 背景は単色のDrawで塗る。ClearRenderTargetView用の固定色は必要ない。
    return Create(desc, D3D12_HEAP_TYPE_DEFAULT, GpuCategory::RenderTarget,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
