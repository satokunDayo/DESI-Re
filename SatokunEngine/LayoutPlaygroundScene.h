#pragma once
// 同じ文字地図を2Dと3Dで使う例。配置処理はSceneLayoutLoader、操作ルールはこのScene。
#include "Scene.h"
#include "SceneLayoutLoader.h"
#include "CharacterMotor.h"
#include "CameraControllers.h"

class LayoutPlaygroundScene final : public Scene
{
public:
    explicit LayoutPlaygroundScene(bool twoDimensional = false);
    void OnEnter(SceneContext&) override;
    void OnUpdate(SceneContext&, const SceneInput&, double) override;
    void OnFixedUpdate(SceneContext&, double) override;
    void OnExit(SceneContext&) noexcept override;
    ScenePresentation GetPresentation() const override;
private:
    void UpdateCamera();
    bool twoDimensional_, paused_ = false, inside_ = false, coverVisible_ = true;
    std::uint64_t enters_ = 0, exits_ = 0;
    std::unique_ptr<World> world_;
    std::unique_ptr<CollisionWorld> collision_;
    LayoutInstance layout_;
    ObjectId player_, trigger_, marker_;
    CharacterMotor motor_;
    Camera camera_;
    FirstPersonCamera firstPerson_;
    SceneInput input_;
};
