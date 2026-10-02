#include "LayoutPlaygroundScene.h"
#include "SceneManager.h"
#include "MeshLoader.h"
#include "TextureLoader.h"
#include <algorithm>
#include <cwchar>

LayoutPlaygroundScene::LayoutPlaygroundScene(bool twoDimensional)
    : twoDimensional_(twoDimensional), motor_(twoDimensional ? 2.5f : 3.5f,
        twoDimensional ? MovementPlane::XY : MovementPlane::XZ) {}

void LayoutPlaygroundScene::OnEnter(SceneContext& services)
{
    world_ = std::make_unique<World>(services.memory, 192);
    collision_ = std::make_unique<CollisionWorld>(services.memory, *world_, 32);
    ObjectFactory factory;
    const auto quad = services.meshes ? services.meshes->LoadPrimitive(PrimitiveShape::Quad) : GpuMeshHandle{};
    const auto box = services.meshes ? services.meshes->LoadPrimitive(PrimitiveShape::Box) : GpuMeshHandle{};
    const auto image = services.textures ? services.textures->Load("Textures/checker.png") : GpuTextureHandle{};

    ObjectDesc tile;
    tile.name = "Tile";
    if (quad) { tile.meshRenderer = MeshRenderer{quad}; tile.meshRenderer->material.texture = image; tile.meshRenderer->material.lit = false; }
    if (!twoDimensional_)
    {
        tile.transform.SetScale({2,2,1});
        // Quadの表は-Z。裏側にだけ薄みを持たせ、回した後も通路側へ飛び出さない。
        tile.collider = Collider::Box({.5f,.5f,.05f}, {0,0,.05f});
    }
    else if (tile.meshRenderer) tile.meshRenderer->material.color = {.3f,.38f,.48f};
    factory.Register("Tile", tile);

    ObjectDesc wall;
    wall.name = "Wall";
    if (const auto mesh = twoDimensional_ ? quad : box)
    { wall.meshRenderer = MeshRenderer{mesh}; wall.meshRenderer->material.texture = image; wall.meshRenderer->material.lit = !twoDimensional_; }
    wall.collider = twoDimensional_ ? Collider::Box({.5f,.5f,.2f}) : Collider::Box({.5f,.5f,.5f});
    if (!twoDimensional_) { wall.transform.SetPosition({0,1.5f,0}); wall.transform.SetScale({2,3,2}); }
    factory.Register("Wall", wall);

    ObjectDesc player;
    player.name = "Player";
    if (const auto mesh = twoDimensional_ ? quad : box)
    { player.meshRenderer = MeshRenderer{mesh}; player.meshRenderer->material.color = {.25f,.65f,1}; player.meshRenderer->material.lit = false; }
    player.transform.SetScale(twoDimensional_ ? DirectX::XMFLOAT3{.5f,.5f,1} : DirectX::XMFLOAT3{.5f,1.2f,.5f});
    // 回転した床の計算誤差で初期めり込みにしないよう、足元を5cm離す。重力はまだ使わない。
    if (!twoDimensional_) player.transform.SetPosition({0,.65f,0});
    player.collider = Collider::Box(twoDimensional_ ? DirectX::XMFLOAT3{.5f,.5f,.1f} : DirectX::XMFLOAT3{.5f,.5f,.5f});
    player.collider->category = 2;
    if (player.meshRenderer) player.meshRenderer->render.visible = twoDimensional_; // 一人称では姿だけ隠す。
    factory.Register("Player", player);

    ObjectDesc marker;
    marker.name = "Marker";
    if (quad) { marker.meshRenderer = MeshRenderer{quad}; marker.meshRenderer->material.color = {.2f,.7f,.3f}; marker.meshRenderer->material.lit = false; }
    if (!twoDimensional_)
    { marker.transform.SetEulerDegrees({90,0,0}); marker.transform.SetScale({1.8f,1.8f,1}); marker.transform.SetPosition({0,.015f,0}); }
    else marker.transform.SetScale({.9f,.9f,1});
    factory.Register("Marker", marker);
    ObjectDesc trigger;
    trigger.name = "InvisibleTrigger";
    trigger.collider = Collider::Box(twoDimensional_ ? DirectX::XMFLOAT3{.45f,.45f,.3f} : DirectX::XMFLOAT3{.9f,1,.9f}, {}, ColliderMode::Trigger);
    trigger.collider->category = 8; trigger.collider->mask = 2;
    if (!twoDimensional_) trigger.transform.SetPosition({0,1,0});
    factory.Register("Trigger", trigger);
    auto foreground = marker;
    foreground.name = "Foreground";
    if (foreground.meshRenderer) foreground.meshRenderer->material.color = {.95f,.5f,.18f};
    factory.Register("Foreground", foreground);

    MapData map;
    MapLayer floor;
    floor.name = "Floor"; floor.placementLayer = 0; floor.plane = GridPlane::XY;
    floor.orderInLayer = 0; floor.symbols.Register('.', "Tile");
    // 自動生成から渡す例。穴掘り法も最終的にこの行配列かFromTextへ渡せばよい。
    const std::vector<std::string> generated(7, std::string(7, '.'));
    std::vector<std::string_view> rows;
    for (const auto& row : generated) rows.push_back(row);
    floor.grid = MapGrid::FromRows(rows);
    if (!twoDimensional_)
    {
        floor.cellSize = {2,2};
        // XYの行は-Yへ進む。+90度で床を上向きにし、地図全体をZ=12へ移す。
        floor.transform.SetEulerDegrees({90,0,0}); floor.transform.SetPosition({0,0,12});
    }
    map.layers.push_back(floor);

    MapLayer walls;
    walls.name = "Walls"; walls.placementLayer = 1;
    walls.plane = twoDimensional_ ? GridPlane::XY : GridPlane::XZ;
    walls.cellSize = twoDimensional_ ? DirectX::XMFLOAT2{1,1} : DirectX::XMFLOAT2{2,2};
    walls.orderInLayer = 1; walls.symbols.Register('#', "Wall"); walls.symbols.Ignore('.');
    // ファイル入力も同じMapGrid。間違いを隠さないため、失敗時に別の地図へ差し替えない。
    walls.grid = MapGrid::FromFile(services.resources, "Maps/layer-room.map");
    map.layers.push_back(walls);

    MapLayer actors;
    actors.name = "Actors"; actors.placementLayer = 2; actors.plane = walls.plane; actors.cellSize = walls.cellSize;
    actors.orderInLayer = 10; actors.symbols.Ignore('.'); actors.symbols.Register('P', "Player");
    actors.grid = MapGrid::FromRows({".......", ".P.....", ".......", ".......", ".......", ".......", "......."});
    map.layers.push_back(actors);

    auto marks = actors;
    marks.name = "Markers"; marks.placementLayer = 3; marks.orderInLayer = 2; marks.symbols = {};
    marks.symbols.Ignore('.'); marks.symbols.Register('T', "Marker");
    marks.grid = MapGrid::FromText(".......\n.......\n.......\n.......\n.......\n.....T.\n.......\n");
    map.layers.push_back(marks);
    auto triggers = marks;
    triggers.name = "Triggers"; triggers.placementLayer = 4; triggers.symbols = {};
    triggers.symbols.Ignore('.'); triggers.symbols.Register('T', "Trigger");
    map.layers.push_back(triggers);

    if (twoDimensional_)
    {
        auto cover = actors;
        cover.name = "Foreground"; cover.placementLayer = 5; cover.orderInLayer = 20; cover.symbols = {};
        cover.symbols.Ignore('.'); cover.symbols.Register('F', "Foreground");
        cover.grid = MapGrid::FromRows({".......", ".......", ".......", ".......", "...F...", ".......", "......."});
        map.layers.push_back(cover);
    }
    else
    {
        auto ceiling = floor;
        ceiling.name = "Ceiling"; ceiling.placementLayer = 5;
        // 同じ板・画像を共有したまま、層全体を下向きにして高さ3へ。UVは板について回る。
        ceiling.transform.SetEulerDegrees({-90,0,0}); ceiling.transform.SetPosition({0,3,0});
        map.layers.push_back(ceiling);
    }
    layout_ = SceneLayoutLoader::Load(*world_, factory, map);
    player_ = layout_.Find(2,1,1); marker_ = layout_.Find(3,5,5); trigger_ = layout_.Find(4,5,5);
    // マップに縛られない配置。2Dでは部屋の横、3Dでは壁沿いの小箱として置く。
    auto independent = factory.MakeDescription("Wall"); independent.name = "IndependentObject";
    independent.transform.SetPosition(twoDimensional_ ? DirectX::XMFLOAT3{8,-3,0} : DirectX::XMFLOAT3{9,.4f,3});
    independent.transform.SetScale({.8f,.8f,.8f});
    (void)world_->Spawn(independent);

    if (twoDimensional_)
    { camera_.SetOrthographic(8,.1f,100); camera_.LookAt({3,-3,-10},{3,-3,0}); camera_.SetDepthEnabled(false); }
    else { camera_.SetPerspective(65,.1f,100); firstPerson_.eyeOffset = {0,.6f,0}; }
    UpdateCamera();
}
void LayoutPlaygroundScene::UpdateCamera()
{ if (world_ && !twoDimensional_) (void)firstPerson_.Apply(*world_, player_, camera_); }
void LayoutPlaygroundScene::OnUpdate(SceneContext& services, const SceneInput& input, double seconds)
{
    input_ = input;
    if (input.cancel) { if (paused_) paused_ = false; else services.scenes.RequestChange("Title"); return; }
    if (input.alternate) { services.scenes.RequestChange(twoDimensional_ ? "Layout3D" : "Layout2D"); return; }
    if (input.pause) paused_ = !paused_;
    if (paused_) return;
    if (input.event)
    {
        coverVisible_ = !coverVisible_;
        for (const auto& entry : layout_.Objects())
            if (entry.layerIndex == 5)
                if (auto* object = world_->Find(entry.id); object && object->meshRenderer)
                    object->meshRenderer->render.visible = coverVisible_;
    }
    if (!twoDimensional_)
    {
        const auto dt = static_cast<float>((std::min)(seconds,.1));
        firstPerson_.Rotate(input.lookX*.15f+input.turnX*90*dt, -input.lookY*.15f+input.turnY*90*dt);
    }
    UpdateCamera();
}
void LayoutPlaygroundScene::OnFixedUpdate(SceneContext&, double seconds)
{
    if (paused_) return;
    DirectX::XMFLOAT3 direction{input_.moveX,input_.moveY,0};
    if (!twoDimensional_)
    {
        const auto forward = firstPerson_.ForwardXZ(), right = firstPerson_.RightXZ();
        direction = {forward.x*input_.moveY+right.x*input_.moveX,0,forward.z*input_.moveY+right.z*input_.moveX};
    }
    (void)motor_.Move(*collision_, player_, direction, seconds);
    for (const auto& event : collision_->Step())
    {
        if (!((event.first == player_ && event.second == trigger_) || (event.first == trigger_ && event.second == player_))) continue;
        if (event.phase == TriggerPhase::Enter) { inside_ = true; ++enters_; }
        else if (event.phase == TriggerPhase::Exit) { inside_ = false; ++exits_; }
    }
    if (auto* object = world_->Find(marker_); object && object->meshRenderer)
        object->meshRenderer->material.color = inside_ ? DirectX::XMFLOAT3{1,.9f,.15f} : DirectX::XMFLOAT3{.2f,.7f,.3f};
    world_->CommitChanges(); UpdateCamera();
}
void LayoutPlaygroundScene::OnExit(SceneContext&) noexcept
{ collision_.reset(); layout_ = {}; world_.reset(); player_ = trigger_ = marker_ = {}; }
ScenePresentation LayoutPlaygroundScene::GetPresentation() const
{
    ScenePresentation view{{.035f,.045f,.075f,1},twoDimensional_ ? L"Layer Map 2D" : L"Layer Map 3D"};
    view.world = world_.get(); view.camera = world_ ? &camera_ : nullptr;
    wchar_t text[320]{};
    swprintf_s(text,L" | %s | WASD Move / F2 2D-3D / E %s / RMB-Arrows Look / P Pause / Esc Title | Trigger E:%llu X:%llu",
        paused_ ? L"Paused" : L"Playing", twoDimensional_ ? L"Foreground" : L"Ceiling",
        static_cast<unsigned long long>(enters_),static_cast<unsigned long long>(exits_));
    view.caption += text; return view;
}
