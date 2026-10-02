#pragma once
// 衝突・移動・追従の使い方を示す小さなScene。エンジン側へ勝敗ルールは埋め込まない。
#include "Scene.h"
#include "StateMachine.h"
#include "CharacterMotor.h"
#include "CameraControllers.h"

class PhysicsPlaygroundScene final : public Scene
{
public:
    PhysicsPlaygroundScene();
    void OnEnter(SceneContext&) override;
    void OnUpdate(SceneContext&,const SceneInput&,double) override;
    void OnFixedUpdate(SceneContext&,double) override;
    void OnExit(SceneContext&) noexcept override;
    ScenePresentation GetPresentation() const override;
private:
    class Playing;
    class Paused;
    void UpdateCamera();
    void MovePlayer(double seconds);
    // Worldを先に宣言して長生きさせる。collision_はWorldを借りているだけ。
    std::unique_ptr<World> world_;
    std::unique_ptr<CollisionWorld> collision_;
    CharacterMotor motor_;
    Camera camera_;
    FollowCamera follow_;
    FirstPersonCamera firstPerson_;
    ObjectId player_,trigger_,marker_;
    SceneContext* services_=nullptr;
    SceneInput input_;
    bool firstPersonView_=false,inside_=false,blocked_=false,overlapping_=false;
    std::uint64_t enters_=0,stays_=0,exits_=0;
    StateMachine<PhysicsPlaygroundScene> modes_;
};
