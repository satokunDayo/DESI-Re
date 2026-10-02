// サンプルの配置と入力ルール。エンジン機能の確認用なので、Enterでクリアする等の仮ルールもここにある。
#include "StarterScenes.h"
#include "PhysicsPlaygroundScene.h"
#include "LayoutPlaygroundScene.h"
#include "TextureLoader.h"
#include "MeshLoader.h"
#include <cwchar>
#include <memory>
#include <new>
#include <utility>

TitleScene::TitleScene(std::string start, std::string alternate)
    : start_(std::move(start)), alternate_(std::move(alternate)) {}

// 画像は入場時に1回読む。毎フレーム読み込み直すと描画より先にロードが仕事になってしまう。
void TitleScene::OnEnter(SceneContext& context)
{
    if (context.textures) image_ = context.textures->Load("Textures/checker.png");
}

// 終了→別ステージ→通常開始の優先順。複数入力があっても切替は1件にする。
void TitleScene::OnUpdate(SceneContext& context, const SceneInput& input, double)
{
    if (input.cancel) context.scenes.RequestQuit();
    else if (input.alternate) context.scenes.RequestChange(alternate_);
    else if (input.confirm) context.scenes.RequestChange(start_);
    else if (input.event) context.scenes.RequestChange("CollisionDemo");
    else if (input.switchCamera) context.scenes.RequestChange("Layout3D");
}

// このチェック柄は仮タイトル画像。WorldもCameraも返さないので3Dの物はまだ描かない。
ScenePresentation TitleScene::GetPresentation() const
{
    ScenePresentation view{ { 0.05f, 0.08f, 0.18f, 1 }, L"Title | Enter: Stage01 / F2: Stage02 / E: Collision / F3: Layer Map / Esc: Quit" };
    if (image_)
    {
        SpriteDraw image;
        image.texture = image_;
        image.x = 320; image.y = 180; image.width = 640; image.height = 360;
        view.sprites.push_back(image);
    }
    return view;
}

// 探索中の振る舞いを置く場所。今は操作確認と回転だけで、歩行や衝突はまだ。
class GameScene::PlayingState final : public IState<GameScene>
{
public:
    void Update(GameScene& game, double) override
    {
        if (game.input_.cancel)
            game.services_->scenes.RequestChange(game.settings_.titleScene);
        else if (game.input_.confirm)
            game.services_->scenes.RequestChange(game.settings_.resultScene);
        else if (game.input_.event) game.modes_.RequestChange("Event");
        else if (game.input_.pause) game.modes_.RequestChange("Paused");
    }
    void FixedUpdate(GameScene& game, double seconds) override
    {
        // Playingでだけ進む時計。EventやPause中に同じSceneを保っても回転を止められる。
        game.Data().playSeconds += seconds;
        if (game.world_)
        {
            if (auto* object = game.world_->Find(game.rotatingObject_))
                object->transform.SetEulerDegrees({ 0, static_cast<float>(game.GetPlaySeconds() * 30.0), 0 });
            // 削除予約は更新の区切りで確定。更新の真っ最中に実体を消さないための出口。
            game.world_->CommitChanges();
        }
    }
};

// Worldを捨てずにイベントだけ進める状態。紙芝居が終わったらPlayingへ戻す。
class GameScene::EventState final : public IState<GameScene>
{
public:
    void Enter(GameScene& game) override { game.event_.Start(game.settings_.eventPages); }
    void Update(GameScene& game, double seconds) override
    {
        game.event_.Update(seconds, game.input_.confirm, game.input_.cancel);
        const auto result = game.event_.GetStatus();
        if (result == EventStatus::Completed || result == EventStatus::Skipped)
        {
            ++game.Data().finishedEvents; // 通常終了・スキップ共通の終了処理。
            game.modes_.RequestChange("Playing");
        }
    }
    void Exit(GameScene& game) noexcept override { game.event_.Cancel(); }
};

// 固定更新を持たないのでプレイ時間と回転が止まる。描画ループそのものは続く。
class GameScene::PausedState final : public IState<GameScene>
{
public:
    void Update(GameScene& game, double) override
    {
        if (game.input_.pause || game.input_.cancel) game.modes_.RequestChange("Playing");
    }
};

// StateMachineのContextはこのGameScene自身。状態クラスから必要なデータへ届く。
GameScene::GameScene(GameSceneSettings settings)
    : settings_(std::move(settings)), modes_(*this)
{
    modes_.Register<PlayingState>("Playing");
    modes_.Register<EventState>("Event");
    modes_.Register<PausedState>("Paused");
}

// サービスを借り、CPUのゲームデータとWorldを作り、最後に最初のStateへ入る。
void GameScene::OnEnter(SceneContext& context)
{
    services_ = &context;
    if (context.textures) image_ = context.textures->Load("Textures/checker.png");
    runtime_ = context.memory.Allocate(ArenaType::Scene,
        sizeof(RuntimeData), alignof(RuntimeData));
    if (!runtime_) throw std::bad_alloc();
    // MemoryBlockは生の領域。RuntimeDataとして使う前に、ここで実際のオブジェクトを構築する。
    std::construct_at(static_cast<RuntimeData*>(runtime_.Data()));
    if (context.meshes)
    {
        // 容量32のWorld。このプールは固定長で、いっぱいになっても勝手には伸びない。
        world_ = std::make_unique<World>(context.memory, 32);
        // 床と2つの立体は同じBoxを共有。違うのは配置と材質で、GPU形状を3回作らない。
        const auto box = context.meshes->LoadPrimitive(PrimitiveShape::Box);
        const auto quad = context.meshes->LoadPrimitive(PrimitiveShape::Quad);
        ObjectDesc object;
        // 薄く伸ばしたBoxを床に見立てる。Solid判定はまだ付いていない、見た目の床。
        object.name = "Ground";
        object.meshRenderer = MeshRenderer{ box };
        object.meshRenderer->material.color = { .45f,.6f,.7f };
        object.meshRenderer->material.texture = image_;
        object.meshRenderer->material.uvScale = { 4,4 };
        object.transform.SetPosition({ 0,-.75f,0 });
        object.transform.SetScale(settings_.sideView ? DirectX::XMFLOAT3{10,.5f,1} : DirectX::XMFLOAT3{8,.5f,8});
        (void)world_->Spawn(object);

        // 同じObjectDescを設定し直して次をSpawn。前に作った床のTransformは書き換わらない。
        object.name = "RotatingObject";
        object.transform.SetPosition({ 0,.5f,0 }); object.transform.SetScale({ 1.5f,2,1.5f });
        object.meshRenderer->material.uvScale = { 1,1 };
        object.meshRenderer->material.color = { 1,.7f,.3f };
        rotatingObject_ = world_->Spawn(object);

        // 画像を外して単色にする例。meshのBoxは共有したまま。
        object.name = "SecondObject"; object.transform.SetPosition({ 2.5f,.25f,1 });
        object.transform.SetScale({ 1,1.5f,1 }); object.meshRenderer->material.texture.reset();
        object.meshRenderer->material.color = { .3f,.8f,.55f };
        (void)world_->Spawn(object);

        // こちらは3D空間に置いた画像板。画面に固定するSpriteとは違ってCameraの影響を受ける。
        object.name = "WorldImage"; object.meshRenderer->mesh = quad;
        object.meshRenderer->material.texture = image_; object.meshRenderer->material.color = { 1,1,1 };
        object.meshRenderer->material.lit = false;
        object.transform.SetScale({ 2,2,1 }); object.transform.SetPosition({ -2.5f,.5f,0 });
        (void)world_->Spawn(object);
        // 視点と投影を切り替えるだけで、同じWorldの物を横から見せられる。
        if (settings_.sideView)
        {
            camera_.LookAt({0,1,-10},{0,1,0}); camera_.SetOrthographic(7,.1f,100);
        }
        else
        {
            camera_.LookAt({6,4,-8},{0,.25f,0}); camera_.SetPerspective(55,.1f,100);
        }
    }
    // 最初だけはUpdate(0)で入場を確定。次の描画時には現在のStateが決まっている。
    modes_.RequestChange(settings_.playIntro ? "Event" : "Playing");
    modes_.Update(0.0);
}

// 今フレームの意味付き入力を保存してからStateへ渡す。
void GameScene::OnUpdate(SceneContext&, const SceneInput& input, double seconds)
{
    input_ = input;
    modes_.Update(seconds);
}

// SceneManagerから来た固定更新を現在のStateへ。Paused/Eventなら回転処理がない。
void GameScene::OnFixedUpdate(SceneContext&, double seconds)
{
    modes_.FixedUpdate(seconds);
}

// StateのExitが使うデータを先に消さない。止める→RuntimeData→World→画像の順。
void GameScene::OnExit(SceneContext&) noexcept
{
    modes_.Stop();
    event_.Cancel();
    if (runtime_)
    {
        std::destroy_at(static_cast<RuntimeData*>(runtime_.Data()));
        runtime_.Reset();
    }
    world_.reset();
    rotatingObject_ = {};
    image_.reset();
    services_ = nullptr;
}

// runtime_内に構築した実体へ戻す。OnEnter前やOnExit後に呼ぶための関数ではない。
GameScene::RuntimeData& GameScene::Data() noexcept
{
    return *static_cast<RuntimeData*>(runtime_.Data());
}

const GameScene::RuntimeData& GameScene::Data() const noexcept
{
    return *static_cast<const RuntimeData*>(runtime_.Data());
}

// まだRuntimeDataがない途中状態でも表示から呼べるよう、空なら0を返す。
double GameScene::GetPlaySeconds() const noexcept
{
    return runtime_ ? Data().playSeconds : 0.0;
}

// 見せる情報だけ組み立てる。ここでは物を回したり時間を進めたりしない。
ScenePresentation GameScene::GetPresentation() const
{
    ScenePresentation view{ settings_.clearColor, settings_.caption };
    const auto& mode = modes_.GetCurrentId();
    view.caption += L" | " + std::wstring(mode.begin(), mode.end());
    if (mode == "Event")
    {
        if (const auto page = event_.GetCurrentPage()) view.caption += L" | " + page->text;
        view.caption += L" | Enter: Next / Esc: Skip";
    }
    else if (mode == "Paused")
    {
        for (int channel = 0; channel < 3; ++channel) view.clearColor[channel] *= 0.35f;
        view.caption += L" | P or Esc: Resume";
    }
    else
    {
        view.caption += L" | E: Event / P: Pause / Enter: Result / Esc: Title";
    }
    wchar_t timing[96]{};
    swprintf_s(timing, L" | Play %.1fs / Events %u", GetPlaySeconds(), runtime_ ? Data().finishedEvents : 0);
    view.caption += timing;
    // WorldとCameraは貸し出すだけ。ScenePresentationが持ち主になるわけではない。
    view.world = world_.get();
    view.camera = world_ ? &camera_ : nullptr;
    if (image_)
    {
        SpriteDraw icon;
        icon.texture = image_;
        icon.x = 24; icon.y = 24; icon.width = icon.height = 64;
        view.sprites.push_back(icon); // 画面座標UI。Worldのカメラに影響されない。
    }
    return view;
}

// 本物のクリア判定はまだ。今は戻るキーだけ受け付ける確認用の結果画面。
void ResultScene::OnUpdate(SceneContext& context, const SceneInput& input, double)
{
    if (input.confirm || input.cancel) context.scenes.RequestChange(next_);
}

// 画像もWorldも持たないので、前SceneのTex/Mesh返却を数字で確認できる。
ScenePresentation ResultScene::GetPresentation() const
{
    return { { 0.25f, 0.12f, 0.03f, 1 }, L"Result | Enter or Esc: Title" };
}

// ここで名前と作り方を結ぶ。登録しただけではOnEnterもGPUロードも走らない。
void RegisterStarterScenes(SceneManager& scenes)
{
    // シーン一覧はここ。Core/Engineにはゲーム固有の型を登録しない。
    scenes.Register<TitleScene>("Title", std::string("Stage01"), std::string("Stage02"));

    GameSceneSettings stage1;
    stage1.caption = L"Stage01";
    stage1.eventPages = {
        { L"Intro page 1: the scene is kept during events.", 0.0, true },
        { L"Intro page 2: wait 1 second, then confirm.", 1.0, true },
    };
    scenes.Register<GameScene>("Stage01", stage1);

    // 共通設定をコピーし、違うところだけ上書き。同じクラスでステージを増やす例。
    auto stage2 = stage1;
    stage2.caption = L"Stage02";
    stage2.clearColor = { 0.05f, 0.24f, 0.16f, 1 };
    stage2.playIntro = false;
    stage2.sideView = true;
    scenes.Register<GameScene>("Stage02", stage2);

    scenes.Register<ResultScene>("Result", std::string("Title"));
    scenes.Register<PhysicsPlaygroundScene>("CollisionDemo");
    scenes.Register<LayoutPlaygroundScene>("Layout3D", false);
    scenes.Register<LayoutPlaygroundScene>("Layout2D", true);
    // 起動直後にTitleなのはこの予約があるから。実際の生成はRun内のScene更新で行う。
    scenes.RequestChange("Title");
}
