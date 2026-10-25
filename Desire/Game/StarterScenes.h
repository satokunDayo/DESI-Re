#pragma once
// 起動ロゴ専用。ゲームのTitleやステージの登録はmain側で行う。
#include "Scene/Scene.h"
#include <string>

class SceneManager;

// ロゴの画像と時計、それから表示後の行き先だけを持つ。
class BootLogoScene final : public Scene
{
public:
    // 行き先が空なら、ロゴ確認用として表示後に終了する。
    explicit BootLogoScene(std::string nextScene = {});
    void OnEnter(SceneContext& context) override;
    void OnUpdate(SceneContext& context, const SceneInput& input, double seconds) override;
    void OnExit(SceneContext&) noexcept override;
    ScenePresentation GetPresentation() const override;

private:
    GpuTextureHandle logo_;
    std::string nextScene_;
    double elapsed_ = 0;
};

// 起動時に1回呼ぶ。trueならロゴ経由、falseなら指定した行き先へ直接進む。
// nextSceneは呼び出す前に登録しておく。空なら本編なしで、終了する設定。
void RegisterStarterScenes(SceneManager& scenes, bool showLogo = false, std::string nextScene = {});
