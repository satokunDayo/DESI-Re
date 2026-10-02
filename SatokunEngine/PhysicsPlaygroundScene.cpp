#include "PhysicsPlaygroundScene.h"
#include "SceneManager.h"
#include "ObjectFactory.h"
#include "MeshLoader.h"
#include <algorithm>
#include <cwchar>

class PhysicsPlaygroundScene::Playing final : public IState<PhysicsPlaygroundScene>
{
public:
    void Update(PhysicsPlaygroundScene& game,double seconds) override
    {
        if(game.input_.cancel){game.services_->scenes.RequestChange("Title");return;}
        if(game.input_.pause){game.modes_.RequestChange("Paused");return;}
        if(game.input_.switchCamera)game.firstPersonView_=!game.firstPersonView_;
        if(game.firstPersonView_)
        {
            // マウスは既に移動量。秒数を掛けるのは押し続ける矢印キーのほうだけ。
            const float secondsClamped=static_cast<float>((std::min)(seconds,.1));
            game.firstPerson_.Rotate(game.input_.lookX*.15f+game.input_.turnX*90*secondsClamped,
                -game.input_.lookY*.15f+game.input_.turnY*90*secondsClamped);
        }
    }
    void FixedUpdate(PhysicsPlaygroundScene& game,double seconds) override {game.MovePlayer(seconds);}
};
class PhysicsPlaygroundScene::Paused final : public IState<PhysicsPlaygroundScene>
{
public:
    void Update(PhysicsPlaygroundScene& game,double) override
    {if(game.input_.pause||game.input_.cancel)game.modes_.RequestChange("Playing");}
};
PhysicsPlaygroundScene::PhysicsPlaygroundScene():modes_(*this)
{
    // 手前の外壁越しでもプレイヤーが見える確認用の角度。
    follow_.offset={0,5,-5};
    modes_.Register<Playing>("Playing");modes_.Register<Paused>("Paused");
}
void PhysicsPlaygroundScene::OnEnter(SceneContext& services)
{
    services_=&services;
    world_=std::make_unique<World>(services.memory,32);
    collision_=std::make_unique<CollisionWorld>(services.memory,*world_,32);
    ObjectFactory factory;
    ObjectDesc solid;
    if(services.meshes)
    {
        solid.meshRenderer=MeshRenderer{services.meshes->LoadPrimitive(PrimitiveShape::Box)};
        solid.collider=Collider::AutoFit();
    }
    else solid.collider=Collider::Box({.5f,.5f,.5f}); // GPUなしのロジック検証でも同じ寸法。
    solid.collider->category=4;
    factory.Register("SolidBox",solid);
    const auto box=[&](const char* name,DirectX::XMFLOAT3 position,DirectX::XMFLOAT3 scale,DirectX::XMFLOAT3 color)
    {
        auto desc=factory.MakeDescription("SolidBox");desc.name=name;
        desc.transform.SetPosition(position);desc.transform.SetScale(scale);
        if(desc.meshRenderer)desc.meshRenderer->material.color=color;
        return world_->Spawn(desc);
    };
    (void)box("Floor",{0,-.75f,0},{12,.5f,12},{.25f,.3f,.36f});
    (void)box("NorthWall",{0,.5f,5.5f},{11.5f,2,.3f},{.45f,.5f,.6f});
    (void)box("SouthWall",{0,.5f,-5.5f},{11.5f,2,.3f},{.45f,.5f,.6f});
    (void)box("WestWall",{-5.5f,.5f,0},{.3f,2,11.5f},{.45f,.5f,.6f});
    (void)box("EastWall",{5.5f,.5f,0},{.3f,2,11.5f},{.45f,.5f,.6f});
    (void)box("ThinWall",{0,.5f,.75f},{4,2,.12f},{.8f,.45f,.18f});

    auto player=factory.MakeDescription("SolidBox");player.name="Player";
    player.transform.SetPosition({0,0,-3});player.transform.SetScale({.6f,1,.6f});
    player.collider->category=2;
    if(player.meshRenderer)player.meshRenderer->material.color={.35f,.65f,1};
    player_=world_->Spawn(player);

    auto marker=factory.MakeDescription("SolidBox");marker.name="TriggerMarker";
    marker.collider.reset();marker.transform.SetPosition({3,-.48f,2});marker.transform.SetScale({2,.04f,2});
    if(marker.meshRenderer){marker.meshRenderer->material.color={.2f,.55f,.25f};marker.meshRenderer->material.lit=false;}
    marker_=world_->Spawn(marker);
    ObjectDesc trigger;trigger.name="InvisibleTrigger";trigger.transform.SetPosition({3,0,2});
    trigger.collider=Collider::Sphere(1.0f,{},ColliderMode::Trigger);
    trigger.collider->category=8;trigger.collider->mask=2; // Playerだけ。床や壁ではイベントを起こさない。
    trigger_=world_->Spawn(trigger);
    camera_.SetPerspective(60,.1f,100);
    modes_.RequestChange("Playing");modes_.Update(0);
    UpdateCamera();
}
void PhysicsPlaygroundScene::OnUpdate(SceneContext&,const SceneInput& input,double seconds)
{input_=input;modes_.Update(seconds);UpdateCamera();}
void PhysicsPlaygroundScene::OnFixedUpdate(SceneContext&,double seconds){modes_.FixedUpdate(seconds);}
void PhysicsPlaygroundScene::UpdateCamera()
{
    if(!world_)return;
    if(auto* player=world_->Find(player_);player&&player->meshRenderer)
        player->meshRenderer->render.visible=!firstPersonView_; // 非表示でもColliderは生きる。
    if(firstPersonView_)(void)firstPerson_.Apply(*world_,player_,camera_);
    else (void)follow_.Apply(*world_,player_,camera_);
}
void PhysicsPlaygroundScene::MovePlayer(double seconds)
{
    DirectX::XMFLOAT3 direction{input_.moveX,0,input_.moveY};
    if(firstPersonView_)
    {
        const auto forward=firstPerson_.ForwardXZ(),right=firstPerson_.RightXZ();
        direction={forward.x*input_.moveY+right.x*input_.moveX,0,forward.z*input_.moveY+right.z*input_.moveX};
    }
    const auto move=motor_.Move(*collision_,player_,direction,seconds);
    blocked_=move.blockedX||move.blockedY||move.blockedZ;overlapping_=move.startedOverlapping;
    // まず全員を移動、次にStep、最後にイベントを処理。Stepの途中でWorldを書き換えない。
    for(const auto& event:collision_->Step())
    {
        if(!((event.first==trigger_&&event.second==player_)||(event.second==trigger_&&event.first==player_)))continue;
        if(event.phase==TriggerPhase::Enter){inside_=true;++enters_;}
        else if(event.phase==TriggerPhase::Stay)++stays_;
        else {inside_=false;++exits_;}
    }
    if(auto* marker=world_->Find(marker_);marker&&marker->meshRenderer)
        marker->meshRenderer->material.color=inside_?DirectX::XMFLOAT3{.85f,.85f,.2f}:DirectX::XMFLOAT3{.2f,.55f,.25f};
    // イベントを受けてRequestDestroyした物も、処理の区切りで片付けられる。
    world_->CommitChanges();UpdateCamera();
}
void PhysicsPlaygroundScene::OnExit(SceneContext&) noexcept
{
    modes_.Stop();collision_.reset();world_.reset();services_=nullptr;
    player_=trigger_=marker_={};
}
ScenePresentation PhysicsPlaygroundScene::GetPresentation() const
{
    ScenePresentation view{{.035f,.055f,.09f,1},L"Collision Demo"};
    view.world=world_.get();view.camera=world_?&camera_:nullptr;
    wchar_t info[384]{};
    const bool paused=modes_.GetCurrentId()=="Paused";
    swprintf_s(info,L" | %s | %s | %s | Trigger E:%llu S:%llu X:%llu%s%s",
        paused?L"Paused":L"Playing",paused?L"P or Esc: Resume":L"WASD Move / F3 Camera / RMB or Arrows Look / P Pause / Esc Title",
        firstPersonView_?L"FPS":L"Follow",
        static_cast<unsigned long long>(enters_),static_cast<unsigned long long>(stays_),static_cast<unsigned long long>(exits_),
        blocked_?L" | Wall":L"",overlapping_?L" | Initial overlap":L"");
    view.caption+=info;return view;
}
