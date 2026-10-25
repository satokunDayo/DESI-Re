// 描画の交通整理。実際の画像用/形状用シェーダー設定は各Rendererへ分担している。
#include "Render/D3D12Renderer.h"
#include <cstdio>
#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
    // HRESULTをここで検査。原因はGPU本体とは限らず、引数・環境・呼び出し順の失敗もある。
    void CheckDX(HRESULT result)
    {
        // 失敗したら即座にキレてプログラムを落とす（例外を投げる） うんうんそれはGPU君が悪いね…じゃ、もっかい実行するね…
        if (FAILED(result))
        {
            char message[80]{};
            sprintf_s(message, "DirectX call failed: HRESULT 0x%08X",
                static_cast<unsigned int>(result));
            throw std::runtime_error(message);
        }
    }
}

//落とす処理 
// メンバーが破棄される前にGPUを待ち、Win32イベントのHANDLEを閉じる。
D3D12Renderer::~D3D12Renderer()
{
    // GPUがまだ描画してるのに電源を引っこ抜くとぶちキレられるので一応声をかける。オーワビサビネ
    if (queue_ && fence_ && fenceEvent_ != nullptr)
    {
        try
        {
            WaitForGpu();
        }
        catch (...)
        {
            // デストラクタで例外を投げられたらもみ消す。
            OutputDebugStringW(L"GPU wait failed during renderer shutdown.\n");
        }
    }

    // フェンス（GPUとの連絡網）の解約手続き
    if (fenceEvent_ != nullptr)
    {
        CloseHandle(fenceEvent_);
    }
}

// Window→Device→描画設定→Queue→SwapChain→命令用メモリ→Fenceの順に準備する。
void D3D12Renderer::Initialize(HWND window)
{
    // 二重初期化防止。起こしに来てくれるツンデレ幼馴染よりも一緒に寝てくれるぽっと出のヒロインが勝つみたいな？
    //↑何言ってんだこいつ

    if (window_ != nullptr)
    {
        throw std::runtime_error("Renderer Initialize must be called once.");
    }

    // 描画キャンバスのサイズ測定
    RECT client{};
    if (!GetClientRect(window, &client) ||
        client.right <= 0 || client.bottom <= 0)
    {
        // 描く場所ないね。。ここは宇宙、感無量なり
        throw std::runtime_error("The window has no valid client area.");
    }

    window_ = window;
    width_ = static_cast<UINT>(client.right);
    height_ = static_cast<UINT>(client.bottom);

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        // バグ発見支援フォーム（D3D12のデバッグレイヤー）
        // 主人公が気づかない、幼馴染の憔悴した心に歩み寄るチャラ男みたいな感じ。いい奴だと願え。人類
        // Debugビルドで利用できるときだけ有効化。Device作成より前でなければならない。
        debug->EnableDebugLayer();
    }
#endif

    // DXGIファクトリーおじさん。グラボやモニターと交渉してくれる。
    CheckDX(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)));

    // DX12 Deviceの召喚。Feature Level 11_0以上を要求し、作れなければ起動失敗にする。
    // 使うAPIはDX12のまま。要求するFeature Levelが11_0なのであって、DX11へ切り替えてはいない。
    HRESULT deviceResult = D3D12CreateDevice(
        nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));

    if (FAILED(deviceResult))
    {
        //GPUがない？ふーん、じゃあ帰ってもらおうか -HAPPY END-
        throw std::runtime_error("Failed to create D3D12 device.");
    }

    // コマンドキュー：GPUへの命令を延々と流す「ベルトコンベア」Hレベル5くらい
    ComPtr<IDXGIAdapter> adapter;
    // 実際に作ったDeviceと同じGPUのAdapterを取得してD3D12MAへ渡す。
    CheckDX(factory_->EnumAdapterByLuid(device_->GetAdapterLuid(), IID_PPV_ARGS(&adapter)));
    gpuMemory_.Initialize(device_.Get(), adapter.Get());
    // この2つでRoot SignatureとPSO、つまり描画の規則一式を用意する。
    spriteRenderer_.Initialize(device_.Get());
    worldRenderer_.Initialize(device_.Get());
    postProcess_.Initialize(device_.Get());
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CheckDX(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)));

    /* スワップチェーン ↓解説より見てわかるほうがいいのかな？

    V-Sync   ┌┐    ┌┐    ┌┐    ┌┐
             ┘└──┘└──┘└──┘└──
    Buffer #0 | GPU  |Screen |  GPU  |Screen |
    Buffer #1 |Screen|  GPU  |Screen |  GPU  |
    表示用画像を2枚使うイメージ。論理更新の回数や間隔はEngine側で別に管理する。
    */

    // ここで決めるのは表示先の画像の規格。ゲームの固定更新60Hzとは別設定。
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = width_;
    swapDesc.Height = height_;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; //フルカラー
    swapDesc.SampleDesc.Count = 1;              // アンチエイリアスなし 
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = BufferCount;         // ダブルバッファ
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 画面切り替えたら古いバッファはポイ捨て

    // Windowsのウィンドウとスワップチェーンを紐付ける
    ComPtr<IDXGISwapChain1> initialSwapChain;
    CheckDX(factory_->CreateSwapChainForHwnd(
        queue_.Get(), window_, &swapDesc, nullptr, nullptr,
        initialSwapChain.GetAddressOf()));
    // 同じCOMオブジェクトから必要なインターフェースを取得。生ポインタの無理な型変換ではない。
    CheckDX(initialSwapChain.As(&swapChain_));

    // 「Alt+Enterで勝手にフルスクリーンになる機能、おで、こいつ、嫌い」
    CheckDX(factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER));

    // デスクリプタヒープ (RTV用) ：キャンバスを飾るための「額縁（View）置き場」
     // ここに絵をセットしないと、GPUはペンキ缶持ったまま立ち尽くす
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = BufferCount;
    CheckDX(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)));

    // 額縁1個あたりのサイズを測っておく。
    // GPUのメーカー（N社とかA社とかI社）によって額縁の厚みが違うんだな
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CreateRenderTargets(); // 画材庫にキャンバスをセットアップ

    // アロケータ：GPUへのおつかいリストを書くためのノート
    CheckDX(device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)));

    // コマンドリスト：ノートから破り取った「1ページ」。ここに「あれ描け」と書き連ねる。
    CheckDX(device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
        IID_PPV_ARGS(&commands_)));

    // とりあえず今は書くことないのでペンを置く。「フッ、スランプだぜ（白紙）」
    CheckDX(commands_->Close());

    // フェンス：「進捗どうですか！？」とGPUを監視する。藤子不二雄かよ
    CheckDX(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));

    // GPUが「終わりました…」って押す専用の呼び鈴（イベント）
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr)
    {
        CheckDX(HRESULT_FROM_WIN32(GetLastError()));
    }
    ApplyRendererSettings();
}

// SwapChain所有の画像を参照し、RTVを作る。これらの画像はD3D12MAでは確保していない。
void D3D12Renderer::CreateRenderTargets()
{
    // 額縁置き場の入り口に立つ
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    // スワップチェーンくんが隠し持ってる裏画面（キャンバス）をカツアゲしてきて、
    // 額縁にバコーン！と嵌め込んでいく作業。
    for (UINT index = 0; index < BufferCount; ++index)
    {
        CheckDX(swapChain_->GetBuffer(index, IID_PPV_ARGS(&backBuffers_[index])));
        device_->CreateRenderTargetView(backBuffers_[index].Get(), nullptr, rtv);
        rtv.ptr += rtvStride_; // 次の額縁へ
    }
    std::uint64_t bytes = 0;
    for (const auto& buffer : backBuffers_)
    {
        const auto desc = buffer->GetDesc();
        bytes += device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    }
    // 外部確保のバックバッファも使用量表示へ含める。深度画像は別途D3D12MAで確保。
    gpuMemory_.SetExternalRenderTargets(bytes, BufferCount);

}

// 旧画面の使用完了→リサイズ→RTVを作り直す。内部画像はApplyRendererSettingsで別に判断。
void D3D12Renderer::Resize(UINT width, UINT height)
{
    // ユーザー「ウィンドウのサイズ変えちゃおｗ びよーんｗｗ」
    // GPU「ふざけんな今描いてんだよ！！座して待たれよ！！」
    //ユーザー「ふむ、かたじけない」
    WaitForGpu();

    // サイズの合わなくなった古いキャンバスを窓から投げ捨てる。
    for (auto& buffer : backBuffers_)
    {
        buffer.Reset();
    }

    // スワップチェーン業者に「新しいサイズのキャンバス大至急持ってこい！」と電話。迷惑なデザイン発注元かな？
    CheckDX(swapChain_->ResizeBuffers(
        BufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    width_ = width;
    height_ = height;

    // 届いた新品のキャンバスを再度額縁に嵌め直す
    CreateRenderTargets();
}

// 1フレームの入口。WorldとCameraはセットで借りる。描画資産はFenceまでRendererも保持。
void D3D12Renderer::DrawFrame(const std::array<float, 4>& clearColor, std::span<const SpriteDraw> sprites,
    const World* world, const Camera* camera, std::span<const SpriteDraw> overlaySprites)
{
    if ((world == nullptr) != (camera == nullptr))
        throw std::invalid_argument("World and Camera must be provided together.");
    if (!swapChain_) throw std::runtime_error("Renderer is not initialized.");
    RECT client{};
    if (!GetClientRect(window_, &client)) throw std::runtime_error("GetClientRect failed.");
    if (IsIconic(window_) || client.right <= 0 || client.bottom <= 0) { Sleep(16); return; }
    const UINT width = static_cast<UINT>(client.right), height = static_cast<UINT>(client.bottom);
    if (width > 16384 || height > 16384) throw std::invalid_argument("Window exceeds renderer size limit.");
    if (width != width_ || height != height_) Resize(width, height);

    Flush(); // 同じAllocatorや内部画像をまだGPUが使っている間は書き換えない。
    ApplyRendererSettings();
    gpuMemory_.BeginFrame();
    const auto holdTextures = [this](std::span<const SpriteDraw> list)
    {
        for (const auto& sprite : list)
            if (sprite.texture)
            {
                if (!gpuMemory_.Owns(*sprite.texture->allocation_))
                    throw std::invalid_argument("Texture belongs to another renderer.");
                inFlightTextures_.push_back(sprite.texture);
            }
    };
    holdTextures(sprites); holdTextures(overlaySprites);
    CheckDX(allocator_->Reset());
    CheckDX(commands_->Reset(allocator_.Get(), nullptr));
    const UINT index = swapChain_->GetCurrentBackBufferIndex();
    auto outputRtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    outputRtv.ptr += static_cast<SIZE_T>(index) * rtvStride_;
    const auto sceneRtv = internalTarget_->Rtv();
    const auto transition = [this](ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = { resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after };
        commands_->ResourceBarrier(1, &barrier);
    };

    try
    {
        // まず小さいキャンバスへ描く。DepthもCameraの縦横比も、この寸法に合わせる。
        transition(internalTarget_->Color(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        // 前の絵は全部描き直す。Placed Resourceの初回使用にも必要な初期化を兼ねる。
        commands_->DiscardResource(internalTarget_->Color(), nullptr);
        commands_->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
        postProcess_.FillBackground(commands_.Get(), internalTarget_->Width(), internalTarget_->Height(), clearColor);
        if (world) DrawWorld(sceneRtv, *world, *camera);
        commands_->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
        const UINT iw = internalTarget_->Width(), ih = internalTarget_->Height();
        const float cw = static_cast<float>(activeSettings_.spriteCanvasWidth ? activeSettings_.spriteCanvasWidth : iw);
        const float ch = static_cast<float>(activeSettings_.spriteCanvasHeight ? activeSettings_.spriteCanvasHeight : ih);
        spriteRenderer_.Draw(commands_.Get(), sprites, iw, ih, cw, ch);
        transition(internalTarget_->Color(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        // 大きいキャンバスへ移る。黒帯を消してから内部画像を拡大し、任意でCRTをかける。
        transition(backBuffers_[index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float black[] = { 0, 0, 0, 1 };
        commands_->ClearRenderTargetView(outputRtv, black, 0, nullptr);
        commands_->OMSetRenderTargets(1, &outputRtv, FALSE, nullptr);
        postProcess_.Draw(commands_.Get(), *internalTarget_, width_, height_, activeSettings_);
        // 読みやすさを保つため、デバッグUIはCRTの後に窓の実ピクセル座標で描く。
        spriteRenderer_.Draw(commands_.Get(), overlaySprites, width_, height_, static_cast<float>(width_), static_cast<float>(height_));
        transition(backBuffers_[index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        CheckDX(commands_->Close());
    }
    catch (...)
    {
        // まだSubmitしていないので、記録したバリアもGPUには届いていない。
        // 次回Resetできるよう閉じる。元の例外はそのまま呼び出し側へ返す。
        (void)commands_->Close();
        throw;
    }
    ID3D12CommandList* lists[] = { commands_.Get() };
    queue_->ExecuteCommandLists(1, lists);
    CheckDX(swapChain_->Present(1, 0));
    WaitForGpu();
}

// Queueの末尾にFence値を書かせ、その値が完了するまでCPU側で待つ。
void D3D12Renderer::WaitForGpu()
{
    // 仕事終わったら、この整理券番号（value）をホワイトボードに書いといて！とGPUに伝言を残す
    const UINT64 value = nextFenceValue_++;
    CheckDX(queue_->Signal(fence_.Get(), value));

    // GPUくん、もう書き終わったかな…？チラッ…
    // UINT64_MAXは完了を喜ぶ値ではなくDevice喪失の可能性。理由を調べて例外にする。
    if (fence_->GetCompletedValue() == UINT64_MAX) CheckDX(device_->GetDeviceRemovedReason());
    if (fence_->GetCompletedValue() < value)
    {
        // まだだった。じゃあ書き終わったらこの呼び鈴（fenceEvent_）鳴らしてね
        CheckDX(fence_->SetEventOnCompletion(value, fenceEvent_));

        // 呼び鈴が鳴るまでCPUは仮眠室で爆睡
        if (WaitForSingleObject(fenceEvent_, INFINITE) != WAIT_OBJECT_0)
        {
            throw std::runtime_error("GPU fence wait failed.");
        }
    }
    if (fence_->GetCompletedValue() == UINT64_MAX) CheckDX(device_->GetDeviceRemovedReason());
    // ここまで来れば預かっていた転送元・画像・形状の所有を外せる。他の所有者がいれば実体は残る。
    inFlightUpload_.reset();
    inFlightTextures_.clear();
    inFlightMeshes_.clear();
}
// 初期化途中でも呼べる待機窓口。必要なQueue/Fence/Eventがそろっている場合だけ待つ。
void D3D12Renderer::Flush()
{
    if (queue_ && fence_ && fenceEvent_ != nullptr) WaitForGpu();
}
