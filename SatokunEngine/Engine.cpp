// 各機能を呼ぶ順番はここ。ゲームの壁や敵を直接作る処理はSceneに置く。
#include "Engine.h"
#include <cwchar>
#include "MemorySystemDemo.h"

// エンジンの産声。
// HINSTANCEを受け取り、ウィンドウを顕現させる。
Engine::Engine(HINSTANCE instance)
    : window_(instance)
{
}

bool Engine::Initialize(int showCommand)
{
    // 「世界（ウィンドウ）」の創造。解像度はHD（1280x720）。
    // ここでコケたらゲーム以前の問題なので、潔く腹を切れ。
    if (!window_.Create(1280, 720, showCommand))
    {
        return false;
    }

    //レンダラーを呼び出してキャンバス（HWND）を渡す。労働だ
    renderer_.Initialize(window_.GetHandle());

    // RunMemorySystemDemo(memory_, resources_); // メモリを再確認するときだけ有効化。

    return true;
}

int Engine::Run()
{
    // ゲームの論理は1/60秒刻み。描画FPSを60に縛る設定ではない。
    constexpr double fixedStep = 1.0 / 60.0;
    // まだ固定更新に使っていない時間の貯金箱。1/60秒たまったら1回更新して引く。
    double accumulator = 0.0;
    double statsSeconds = 0.0;
    double fps = 0.0;
    unsigned int frames = 0;
    unsigned int resolutionPreset = 0; // F7で確認するサンプル用の番号。
    std::wstring previousTitle;
    std::string reportedError;
    // 起動準備にかかった時間を1フレーム目の移動量へ混ぜない。ここから計測開始。
    timer_.Reset();

    // OSの通知を処理してからゲームの番。×ボタンの終了通知が来たらこのループを抜ける。
    while (window_.ProcessMessages())
    {
        timer_.Tick();
        const double realDelta = timer_.GetDeltaSeconds();
        // 復帰した瞬間の入力や長い経過時間を、そのままゲームへ投げ込まないために前の状態も見る。
        const bool wasActive = input_.IsActive();
        input_.Poll(window_.GetHandle());
        const bool active = input_.IsActive() && wasActive;
        // イベントの待ち時間には実時間、ゲームの追いつき更新には最大0.1秒を使う。
        const double uiDelta = active ? realDelta : 0.0;
        const double gameDelta = active ? (realDelta > 0.1 ? 0.1 : realDelta) : 0.0;

        // 描画確認用。F5は今後の外部データ再読み込み用に空けておく。
        if (input_.GetKeyDown(VK_F6))
            renderer_.SetCrtQuality(renderer_.GetRendererSettings().crt.quality == CrtQuality::None
                ? CrtQuality::Fast : CrtQuality::None);
        if (input_.GetKeyDown(VK_F7))
        {
            resolutionPreset = (resolutionPreset + 1) % 5;
            constexpr UINT sizes[4][2] = { {320,180}, {400,240}, {640,360}, {854,480} };
            if (resolutionPreset == 4) renderer_.SetNativeResolution();
            else renderer_.SetInternalResolution(sizes[resolutionPreset][0], sizes[resolutionPreset][1]);
        }

        // Enter等のキー番号を「決定」へ翻訳。Sceneはキーボードの配列まで知らなくていい。
        SceneInput actions;
        actions.confirm = input_.GetKeyDown(VK_RETURN) || input_.GetKeyDown(VK_SPACE);
        actions.cancel = input_.GetKeyDown(VK_ESCAPE);
        actions.pause = input_.GetKeyDown('P');
        actions.event = input_.GetKeyDown('E');
        actions.alternate = input_.GetKeyDown(VK_F2);
        if (active)
        {
            actions.moveX=float(input_.GetKey('D'))-float(input_.GetKey('A'));
            actions.moveY=float(input_.GetKey('W'))-float(input_.GetKey('S'));
            const auto mouse=input_.GetMouseDelta();
            actions.lookX=static_cast<float>(mouse.x);actions.lookY=static_cast<float>(mouse.y);
            actions.turnX=float(input_.GetKey(VK_RIGHT))-float(input_.GetKey(VK_LEFT));
            actions.turnY=float(input_.GetKey(VK_UP))-float(input_.GetKey(VK_DOWN));
            actions.switchCamera=input_.GetKeyDown(VK_F3);
        }
        // シーン切替と状態更新を先に処理する。終了要求が出たら描画まで進まない。
        scenes_.Update(actions, uiDelta);
        if (scenes_.IsQuitRequested()) break;

        // 切替中の旧ステージへ、たまった固定更新を流し込まない。時間の貯金も捨てる。
        if (!active || scenes_.GetStatus() != SceneStatus::Ready || scenes_.IsTransitionPending())
            accumulator = 0.0;
        else accumulator += gameDelta;
        // 30FPSならだいたい2回、144FPSなら0回のフレームもある。描画回数と論理更新は別。
        while (accumulator >= fixedStep && !scenes_.IsTransitionPending() && !scenes_.IsQuitRequested())
        {
            FixedUpdate(fixedStep);
            accumulator -= fixedStep;
        }

        // 補間率の受け渡し口はある。ただし今のRenderではまだ使っていない。
        Render(accumulator / fixedStep);
        // 使われなくなったCPUキャッシュを経過時間で回収。GPUのFence待ちとは別のお片付け。
        resources_.Update(realDelta);
        // もう実体がないweak目録を掃除する。生きている画像や形状を強制削除する処理ではない。
        textures_.PruneExpired();
        meshes_.PruneExpired();
        ++frames;
        statsSeconds += realDelta;
        // 表示FPSは約0.5秒分の平均。GPU単体の速度を測っている数字ではない。
        if (statsSeconds >= 0.5)
        {
            fps = frames / statsSeconds;
            frames = 0;
            statsSeconds = 0.0;
        }
        const auto view = scenes_.GetPresentation();
        // ここのCPU表示はSceneArenaの予約量。SystemやWindowsやSTLまで全部足したRAM量ではない。
        const auto stats = memory_.GetArena(ArenaType::Scene)->GetSnapshot().blocks;
        const auto gpu = renderer_.GetGpuStats();
        wchar_t suffix[384]{};
        swprintf_s(suffix, L" | %.0f FPS | CPU %.1f KiB | GPU Tex %.2f/%.0f MiB | Mesh %.2f | RT %.1f | Upload %.2f | Heap %.1f | OS(local) %.0f/%.0f MiB",
            fps, static_cast<double>(stats.reservedBytes) / BlockAllocator::KiB,
            static_cast<double>(gpu.allocationBytes[0]) / GpuMiB, static_cast<double>(gpu.targetBytes[0]) / GpuMiB,
            static_cast<double>(gpu.allocationBytes[1]) / GpuMiB, static_cast<double>(gpu.allocationBytes[2]) / GpuMiB,
            static_cast<double>(gpu.allocationBytes[3]) / GpuMiB, static_cast<double>(gpu.allocatorBlockBytes) / GpuMiB,
            static_cast<double>(gpu.localUsageBytes) / GpuMiB, static_cast<double>(gpu.localBudgetBytes) / GpuMiB);
        const auto drawing = renderer_.GetActiveRendererSettings();
        wchar_t display[128]{};
        swprintf_s(display, L" | %ux%u%s | CRT %s | F6: CRT / F7: Resolution",
            renderer_.GetInternalWidth(), renderer_.GetInternalHeight(),
            drawing.resolution.mode == ResolutionMode::Native ? L" Native" : L"",
            drawing.crt.quality == CrtQuality::Fast ? L"Fast" : L"Off");
        const auto title = std::wstring(L"DESI:Re | ") + view.caption + display + suffix;
        // 同じタイトルを毎回OSへ送り直す必要はない。変わったときだけ書き換える。
        if (title != previousTitle)
        {
            SetWindowTextW(window_.GetHandle(), title.c_str());
            previousTitle = title;
        }
        // 同じエラーを延々と出力窓へ連射しない。内容が変わったときだけ報告。
        if (scenes_.GetLastError() != reportedError)
        {
            reportedError = scenes_.GetLastError();
            if (!reportedError.empty())
            {
                OutputDebugStringA(reportedError.c_str());
                OutputDebugStringA("\n");
            }
        }
        // 裏へ回ったときまで全力疾走させない。しばらく寝てCPUを空ける。
        if (!input_.IsActive()) Sleep(16);
    }

    // 必ずSceneを先に終了。Renderer/MemoryManagerはまだ生きている。
    scenes_.Shutdown();
    // Sceneの所有を外した後で、Systemを含むCPUキャッシュとArenaも閉じ始める。
    resources_.BeginShutdown();
    // 借り物が残っているなら無理に消さない。まだ誰かが持っている、と出力窓へ知らせる。
    if (!memory_.TryFinishShutdown())
        OutputDebugStringW(L"CPU memory is still held by a handle or an operation.\n");
    return 0;
}

// 固定更新の中身は今いるSceneへ。Engineに個別ゲームのルールを埋め込まない。
void Engine::FixedUpdate(double deltaSeconds)
{
    scenes_.FixedUpdate(deltaSeconds);
}

// Sceneが持つWorld/Cameraをこのフレームだけ借り、描画内容をRendererへ渡す。
void Engine::Render(double)
{
    const auto view = scenes_.GetPresentation();
    renderer_.DrawFrame(view.clearColor, view.sprites, view.world, view.camera, view.overlaySprites);
}