#pragma once
// GPU用の確保と予算の窓口。CPUのBlockAllocatorをVRAMへそのまま流用してはいない。
#include <d3d12.h>
#include <dxgi1_4.h>
#include <array>
#include <cstdint>
#include <memory>

// 画像・形状・描画先・転送用に用途を分類。UploadはGPUへ渡す途中のCPU可視メモリ。
enum class GpuCategory : std::size_t { Texture, Mesh, RenderTarget, Upload, Count };
inline constexpr std::uint64_t GpuMiB = 1024ull * 1024;

// 確保中リソース、Allocatorの大元のヒープ、OSの使用量は別の数え方。単純に全部足さない。
struct GpuMemoryStats
{
    // 分類ごとの実割当サイズ。画像の幅×高さ×4とは配置の都合で一致しないことがある。
    std::array<std::uint64_t, 4> allocationBytes{};
    std::array<std::uint64_t, 4> targetBytes{};
    std::array<std::size_t, 4> allocationCount{};
    std::uint64_t allocatorBlockBytes = 0; // 空き部分を含むD3D12MAのヒープ
    std::uint64_t localUsageBytes = 0;     // OSが報告するプロセス使用量
    std::uint64_t localBudgetBytes = 0;    // OSの予算。専用VRAM容量とは別
    std::uint64_t nonLocalUsageBytes = 0;
    std::uint64_t nonLocalBudgetBytes = 0;
};

struct GpuMemoryState;
namespace D3D12MA { class Allocation; }

// GPU使用完了後に破棄する。Allocatorも一緒に生存させるRAII所有者。
// 今回はRendererが転送・描画の各呼び出しでFenceを待つ。
class GpuAllocation final
{
public:
    ~GpuAllocation();
    GpuAllocation(const GpuAllocation&) = delete;
    GpuAllocation& operator=(const GpuAllocation&) = delete;
    // 借りたCOMポインタを返す。勝手にReleaseせず、このGpuAllocationを生かして使う。
    ID3D12Resource* Resource() const noexcept;
    std::uint64_t Size() const noexcept;
private:
    friend class GpuMemoryManager;
    GpuAllocation(std::shared_ptr<GpuMemoryState> state, GpuCategory category);
    // 確保元のAllocatorとDeviceも生かす。リソースだけ残して大家を先に消さない。
    std::shared_ptr<GpuMemoryState> state_;
    GpuCategory category_;
    D3D12MA::Allocation* allocation_ = nullptr;
};
// これは別クラスではなく共有所有の別名。最後の所有が外れたときGpuAllocationが片付く。
using GpuBuffer = std::shared_ptr<GpuAllocation>;

// GPU側はメインスレッド専用。CPUのMemoryManagerとは別の窓口。
class GpuMemoryManager final
{
public:
    // 実際のヒープ分割はMITライブラリD3D12MAへ任せ、こちらで分類と寿命を管理する。
    void Initialize(ID3D12Device* device, IDXGIAdapter* adapter);
    void BeginFrame();
    // DEFAULTならMesh、UPLOADならUploadの分類を使う。任意用途を何でも通すAPIではない。
    GpuBuffer CreateBuffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap,
        GpuCategory category, D3D12_RESOURCE_STATES initialState);
    // 現在はRGBA8・1枚・1ミップのみ。圧縮形式やミップ列は今後の追加対象。
    std::shared_ptr<GpuAllocation> CreateTexture(const D3D12_RESOURCE_DESC& desc);
    // 描画先と同じ寸法の深度画像を作る。内部解像度での前後判定に使う。
    std::shared_ptr<GpuAllocation> CreateDepth(UINT width, UINT height);
    // 内部描画先。RTVへ描き、SRVとして拡大時に読む。RTカテゴリへ計上する。
    std::shared_ptr<GpuAllocation> CreateColorTarget(UINT width, UINT height);
    GpuMemoryStats GetStats() const;
    // カテゴリ別の上限を設定する。設定変更だけで既存リソースを追い出す機能はない。
    void SetTarget(GpuCategory category, std::uint64_t bytes);
    // SwapChain側が所有するバックバッファは、自分では確保せず統計だけ足す。
    void SetExternalRenderTargets(std::uint64_t bytes, std::size_t count);
    // 別Rendererのリソースを誤って描かないよう、同じ管理元から生まれたか確認する。
    bool Owns(const GpuAllocation& allocation) const noexcept;
private:
    std::shared_ptr<GpuAllocation> Create(const D3D12_RESOURCE_DESC& desc,
        D3D12_HEAP_TYPE heap, GpuCategory category, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear = nullptr);
    std::shared_ptr<GpuMemoryState> state_;
};

// HRESULTの失敗を例外へ変換する共通関数。
void CheckGpu(HRESULT result);
