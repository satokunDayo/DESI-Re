// 起動ロゴだけを扱う。ゲーム本編のクラスや進行ルールはここへ入れない。
#include "Game/StarterScenes.h"
#include "Scene/SceneManager.h"
#include "Resource/TextureLoader.h"
#include <utility>

BootLogoScene::BootLogoScene(std::string nextScene)
    : nextScene_(std::move(nextScene))
{
}

void BootLogoScene::OnEnter(SceneContext& context)
{
    elapsed_ = 0;
    if (context.textures)
    {
        logo_ = context.textures->Load("Textures/desire-logo.png");
    }
}

void BootLogoScene::OnUpdate(SceneContext& context, const SceneInput& input, double seconds)
{
    // Escはアプリ終了。Enterでのスキップとは分ける。
    if (input.cancel)
    {
        context.scenes.RequestQuit();
        return;
    }

    elapsed_ += seconds;
    if (input.confirm || elapsed_ >= 10)
    {
        // 本編がまだなければ終了。指定済みなら、登録されたSceneへ切替を予約する。
        if (nextScene_.empty()) context.scenes.RequestQuit();
        else context.scenes.RequestChange(nextScene_);
    }
}

void BootLogoScene::OnExit(SceneContext&) noexcept
{
    logo_.reset();
}

ScenePresentation BootLogoScene::GetPresentation() const
{
    ScenePresentation view{ { 1, 1, 1, 1 }, L"BootLogo | Enter: Skip / Esc: Quit" };
    if (logo_)
    {
        // 128x128の元絵を正方形で表示。最近傍サンプルは既存SpriteRendererが担当。
        SpriteDraw image;
        image.texture = logo_;
        image.x = 384;
        image.y = 104;
        image.width = image.height = 512;
        view.sprites.push_back(image);
    }
    return view;
}

void RegisterStarterScenes(SceneManager& scenes, bool showLogo, std::string nextScene)
{
    if (showLogo)
    {
        // ロゴの作り方に行き先を添える。この時点では画像をまだ読まない。
        scenes.Register<BootLogoScene>("BootLogo", std::move(nextScene));
        scenes.RequestChange("BootLogo");
    }
    else if (!nextScene.empty())
    {
        // ロゴなしの場合は、mainで登録したゲームの開始先へ直接入る。
        scenes.RequestChange(nextScene);
    }
    else
    {
        // ロゴも本編もない設定なら、空のウィンドウを出さずに終える。
        scenes.RequestQuit();
    }
}
