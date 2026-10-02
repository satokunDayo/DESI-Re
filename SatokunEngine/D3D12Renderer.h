#pragma once
// DX12描画の総元締め。窓は借り、GPUの命令・画面・転送中の資産を所有する。

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include "SpriteRenderer.h"
#include "WorldRenderer.h"
#include "PostProcessRenderer.h"
#include <vector>
struct CpuImage;

class D3D12Renderer final
{
public:
    D3D12Renderer() = default;
    ~D3D12Renderer();

    // コピー禁止
    // GPUリソースという俺の嫁をコピーして共有（NTR）されるのを防ぐための純愛儀式 
    // ↑過激派の同担拒否かな？

    // 禁止しているのはRenderer本体のコピー。画像や形状のHandleは、むしろ意図して共有する。
    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    // DX12初期化
    // デバイス（御神体）を召喚
    // Windowを作った後に1回呼ぶ。借りたHWNDの破棄はWindow側の仕事。
    void Initialize(HWND window);

    // 1フレーム描画
    // CPUとGPUが命を削って1枚描く。FPSは表示環境と処理時間しだい。
    // 内部画像へWorld/Sprite→拡大/CRT→overlay→表示→GPU待ち。メインスレッド専用。
    void DrawFrame(const std::array<float, 4>& clearColor, std::span<const SpriteDraw> sprites = {}, const World* world = nullptr, const Camera* camera = nullptr, std::span<const SpriteDraw> overlaySprites = {});
    // CPU画素をGPUへコピーし終えてから返す。呼び出し後は元のCpuImageを片付けてよい。
    GpuTextureHandle UploadImage(const CpuImage& image);
    // 同じく頂点とインデックスを転送する。実装はD3D12Renderer.World.cppへ分けてある。
    GpuMeshHandle UploadMesh(const CpuMesh& mesh);
    // 統計のコピーを返す。SetGpuTargetは分類ごとの目標設定へつなぐ。
    GpuMemoryStats GetGpuStats() const { return gpuMemory_.GetStats(); }
    void SetGpuTarget(GpuCategory category, std::uint64_t bytes) { gpuMemory_.SetTarget(category, bytes); }
    // Queueへ出した仕事が終わるまで待つ。Scene切替や同じ命令メモリの再利用前に呼ぶ。
    void Flush();

    // 設定の予約。Initialize前も指定可能で、描画中の画像をその場で消したりしない。
    void SetRendererSettings(const RendererSettings& settings);
    void SetInternalResolution(UINT width, UINT height);
    void SetNativeResolution();
    void SetCrtQuality(CrtQuality quality);
    void SetSpriteCanvasSize(UINT width, UINT height); // 0,0なら内部サイズを使う
    RendererSettings GetRendererSettings() const { return requestedSettings_; }
    RendererSettings GetActiveRendererSettings() const { return activeSettings_; }
    UINT GetInternalWidth() const noexcept { return internalTarget_ ? internalTarget_->Width() : 0; }
    UINT GetInternalHeight() const noexcept { return internalTarget_ ? internalTarget_->Height() : 0; }

private:
    // 長ったらしいMicrosoft::WRL::ComPtrを短縮する魔法。
    // 参照カウントを自動管理してくれるありがたい存在
    template<class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    // 表示用キャンバスを2枚用意する。現状は各フレーム末尾でGPU完了を待つ。
    // それがアイドル
    // 表示用は2枚。ただし命令Allocatorは1個なので、CPU/GPUの複数フレーム並行実行はしていない。
    static constexpr UINT BufferCount = 2;

    // 裏画面（キャンバス）をカツアゲしてきて、額縁（RTV）にバコーン！と嵌め込むだけのお仕事
    void CreateRenderTargets();
    // Flush後に呼ぶ。内部カラー/深度が両方できたら設定を有効化する。
    void ApplyRendererSettings();
    // Worldの描画一覧を借り、使うMeshとTextureをGPU完了まで保持する。
    void DrawWorld(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const World& world, const Camera& camera);

    // ユーザーが「画面ちっちゃｗ」とか言ってウィンドウをビヨーンってした時に、キレるメソッド。
    void Resize(UINT width, UINT height);

    // GPU完了待機
    // これをサボると...なんかやばいらしい
    // 待つ前にリソースやAllocatorを再利用すると、GPUが読んでいる途中のデータを壊す可能性がある。
    void WaitForGpu();

    // 描画対象のウィンドウ。お前の推しを移すありがたいパイプライン
    HWND window_ = nullptr;

    // ウィンドウのクライアント解像度。内部解像度はinternalTarget_が持つ。
    // ユーザーの気まぐれでコロコロ変わる。世知辛い。ジャッジメントデイは近い。
    UINT width_ = 0;
    UINT height_ = 0;

    // 額縁の厚み。GPUメーカーによって厚みが違うから先に諮るらしい。味も違うのかな？
    UINT rtvStride_ = 0;

    // DXGIファクトリーおじさん。グラボやモニターと交渉してくれる。
    ComPtr<IDXGIFactory4> factory_;

    // スワップチェイン
    // 「表画面」と「裏画面」をシュバババッ！とすり替えて描画
    ComPtr<IDXGISwapChain3> swapChain_;

    // GPUデバイス本体
    // ご機嫌を損ねると真っ暗な画面に俺たちが反射する
    ComPtr<ID3D12Device> device_;

    // GPUへコマンドを送るキューエンドレス絶望ベルトコンベア。
    ComPtr<ID3D12CommandQueue> queue_;

    // コマンドメモリ管理（アロケータ）
    //命令を書くためのノート(メモリ)
    ComPtr<ID3D12CommandAllocator> allocator_;

    // コマンドリスト：ノートから破り取った「1ページ」。ここに「あれ描け」と書き連ねる。
    ComPtr<ID3D12GraphicsCommandList> commands_;

    // デスクリプタヒープ (RTV用) ：キャンバスを飾るための「額縁（View）置き場」
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;

    // スワップチェインのバックバッファたち
    std::array<ComPtr<ID3D12Resource>, BufferCount> backBuffers_;


    // CPUとGPU同期用Fence
    // フェンス：「進捗どうですか！？」とGPUを監視する。藤子不二雄かよ
    ComPtr<ID3D12Fence> fence_;

    // Fence完了通知イベント
    // GPUが「終わりました…」って押す専用の呼び鈴（イベント）
    HANDLE fenceEvent_ = nullptr;

    // 次に発行するFence値（整理券番号）
    // 病院の待合室みたいに「次、1番の方～」「2番の方～」と増えていく。
    // UINT64は18京くらい、97億年くらいでオーバーフローするかな？
    UINT64 nextFenceValue_ = 1;

    // 宣言の逆順に破棄。GPU完了待ち後に画像を返し、最後にAllocatorを破棄する。
    // 宣言順も寿命設計の一部。メンバーは下から上へ破棄される。
    GpuMemoryManager gpuMemory_;
    SpriteRenderer spriteRenderer_;
    // CPU側のSceneが所有を外しても、GPUの処理が終わるまではRendererが預かる。
    std::vector<GpuTextureHandle> inFlightTextures_;
    // 転送元もコピー完了まで必要。ExecuteCommandListsは完了を待ってくれる関数ではない。
    GpuBuffer inFlightUpload_;
    WorldRenderer worldRenderer_;
    std::vector<GpuMeshHandle> inFlightMeshes_;
    // GPU完了後に破棄される内部画像一式。ウィンドウのリサイズとは独立して持つ。
    PostProcessRenderer postProcess_;
    std::unique_ptr<InternalRenderTarget> internalTarget_;
    RendererSettings requestedSettings_;
    RendererSettings activeSettings_;
};