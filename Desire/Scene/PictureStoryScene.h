#pragma once
#include "Scene/Scene.h"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// フォルダ内のPNGを、ファイル名順にめくる紙芝居。
// オープニングもエンディングも、フォルダと行き先を替えて使う。
class PictureStoryScene final : public Scene
{
public:
    PictureStoryScene(
        std::filesystem::path folder,
        std::string nextScene = {},
        bool allowSkip = true);

    void OnEnter(SceneContext& context) override;

    void OnUpdate(
        SceneContext& context,
        const SceneInput& input,
        double seconds) override;

    void OnExit(SceneContext& context) noexcept override;

    ScenePresentation GetPresentation() const override;

private:
    void ShowPage(SceneContext& context, std::size_t index);
    void Finish(SceneContext& context);

    // 作成時に受け取る設定。
    std::filesystem::path folder_; // Assetsから見たフォルダの場所。
    std::string nextScene_;        // 終わったら移るシーン。空なら終了。
    bool allowSkip_;               // F2で紙芝居全体を飛ばせるか。

    // 実行中に使うデータ。
    std::vector<std::filesystem::path> pages_; // 画像の住所録。
    std::size_t pageIndex_ = 0;                // 今、何枚目か。
    GpuTextureHandle page_;                   // 今表示しているGPU画像。
};