#include "Game/Stage1Scene.h"
#include "Game/CollisionCategories.h"
#include "Game/Algorithm/MazeAlgorithmSatokunSpecial.h"
#include "Resource/MeshLoader.h"
#include "Model/ModelLoader.h"
#include "Resource/TextureLoader.h"
#include "Scene/SceneManager.h"
#include "World/SceneLayoutLoader.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

Stage1Scene::Stage1Scene(MazeStageSettings settings)
    : settings_(std::move(settings))
{
    if (settings_.saveFile.has_root_path())
        throw std::invalid_argument("Maze save file must be relative to Assets.");
    for (const auto& part : settings_.saveFile)
        if (part == "..") throw std::invalid_argument("Maze save file cannot contain '..'.");
}

void Stage1Scene::OnEnter(SceneContext& context)
{
    if (!context.meshes || !context.textures || !context.models)
        throw std::runtime_error("Stage1 needs MeshLoader, TextureLoader and ModelLoader.");

    // ここはシーン入場時だけ。コンティニューでは呼ばない。
    MazeAlgorithmSatokunSpecial generator(
        '#', '.', 'P', settings_.width, settings_.height);

    if (!settings_.saveFile.empty())
    {
        const auto output = context.resources.GetRootPath() / settings_.saveFile;
        std::filesystem::create_directories(output.parent_path());
        generator.setFilename(output);
    }
    generator.generate(settings_.extraHoles);
    grid_ = MapGrid::FromText(generator.getMapText());
    const auto& grid = grid_;

    // FBXの形・材質・アニメを読み、部位数も取得する。入場時の1回だけ。
    const auto enemyModel = context.models->Load("PoteraEnemy.fbx");

    constexpr float cellSize = 2.0f;
    constexpr float wallHeight = 2.5f;
    // 偶数補正が済んだ実寸から席数を決める。
    world_ = std::make_unique<World>(
        context.memory, grid.Width() * grid.Height() * 3 + 8
        + enemyModel->Parts().size() + 1); // 敵の各部位と根元の席。

    const auto quad = context.meshes->LoadPrimitive(PrimitiveShape::Quad);
    const auto box = context.meshes->LoadPrimitive(PrimitiveShape::Box);
    const auto floorTexture = context.textures->Load("Textures/FloorAndRoofBrick.png");
    const auto wallTexture = context.textures->Load("Textures/WallBrick.png");

    ObjectFactory factory;
    ObjectDesc floor;
    floor.name = "Floor";
    floor.meshRenderer.emplace();
    floor.meshRenderer->mesh = quad;
    floor.meshRenderer->material.texture = floorTexture;
    floor.transform.SetScale({ cellSize, cellSize, 1 });
    floor.transform.SetEulerDegrees({ 90, 0, 0 });
    factory.Register("Floor", floor);

    ObjectDesc ceiling = floor;
    ceiling.name = "Ceiling";
    ceiling.transform.SetEulerDegrees({ -90, 0, 0 });
    ceiling.transform.SetPosition({ 0, wallHeight, 0 });
    factory.Register("Ceiling", ceiling);

    // ゴールの見た目は明るい水色。Triggerとは別オブジェクトにする。
    ObjectDesc goalFloor = floor;
    goalFloor.name = "GoalFloor";
    goalFloor.meshRenderer->material.texture.reset();
    goalFloor.meshRenderer->material.color = { 0.0f, 1.0f, 1.0f };
    goalFloor.meshRenderer->material.lit = false;
    factory.Register("GoalFloor", goalFloor);

    ObjectDesc wall;
    wall.name = "Wall";
    wall.meshRenderer.emplace();
    wall.meshRenderer->mesh = box;
    wall.meshRenderer->material.texture = wallTexture;
    wall.transform.SetScale({ cellSize, wallHeight, cellSize });
    wall.transform.SetPosition({ 0, wallHeight * 0.5f, 0 });
    wall.collider = Collider::AutoFit();
    wall.collider->category = GameCollision::Wall;
    wall.collider->mask = GameCollision::Player;
    factory.Register("Wall", wall);

    ObjectDesc player;
    player.name = "Player";
    player.collider = Collider::Box({ 0.3f, 0.9f, 0.3f }, { 0, 0.9f, 0 });
    player.collider->category = GameCollision::Player;
    // 敵側だけ許可しても駄目。プレイヤー側にもEnemyを足す。
    player.collider->mask = GameCollision::Wall | GameCollision::Goal | GameCollision::Enemy;
    factory.Register("Player", player);

    // 画像もMeshも持たない。Gの床の上に判定だけ置く。
    ObjectDesc goal;
    goal.name = "Goal";
    goal.collider = Collider::Box(
        { 0.65f, 0.9f, 0.65f }, { 0, 0.9f, 0 }, ColliderMode::Trigger);
    goal.collider->category = GameCollision::Goal;
    goal.collider->mask = GameCollision::Player;
    factory.Register("Goal", goal);

    const auto makeLayer = [&](const char* name, int number)
        {
            MapLayer layer;
            layer.name = name;
            layer.placementLayer = number;
            layer.grid = grid;
            layer.plane = GridPlane::XZ;
            layer.cellSize = { cellSize, cellSize };
            return layer;
        };

    auto floors = makeLayer("Floors", 0);
    auto ceilings = makeLayer("Ceilings", 1);

    for (char symbol : { '#', '.', 'P' })
        floors.symbols.Register(symbol, "Floor");

    floors.symbols.Register('G', "GoalFloor");

    for (char symbol : { '#', '.', 'P', 'G' })
        ceilings.symbols.Register(symbol, "Ceiling");

    auto walls = makeLayer("Walls", 2);
    walls.symbols.Register('#', "Wall");

    for (char symbol : { '.', 'P', 'G' })
        walls.symbols.Ignore(symbol);

    auto actors = makeLayer("Actors", 3);
    actors.symbols.Register('P', "Player");
    actors.symbols.Register('G', "Goal");
    actors.symbols.Ignore('#');
    actors.symbols.Ignore('.');

    MapData map;
    map.layers = { floors, ceilings, walls, actors };
    const auto layout = SceneLayoutLoader::Load(*world_, factory, map);

    std::size_t playerCount = 0, goalCount = 0;
    for (const auto& placed : layout.Objects())
    {
        if (placed.layerIndex != 3) continue;
        if (placed.symbol == 'P') { player_ = placed.id; ++playerCount; }
        if (placed.symbol == 'G') { goal_ = placed.id; ++goalCount; }
    }
    if (playerCount != 1 || goalCount != 1)
        throw std::runtime_error("Maze needs exactly one P and one G.");

    startPosition_ = world_->Find(player_)->transform.GetPosition();

    // 地図を借りて敵が独自に配置候補を選ぶ。モデルの読み込みはここまでの1回。
    enemy_ = std::make_unique<EnemyCharacter>(
        *world_, enemyModel, grid_, cellSize, startPosition_);

    collision_ = std::make_unique<CollisionWorld>(context.memory, *world_);
    camera_.SetPerspective(65.0f, 0.05f, 100.0f);
    cameraControl_.eyeOffset = { 0, 1.6f, 0 };
    ResetRound();
}

void Stage1Scene::ResetRound()
{
    // Worldを作り直さない。迷路・GPU画像・形状は同じものを使う。
    auto* player = world_->Find(player_);
    if (!player) throw std::logic_error("Cannot reset a missing player.");
    player->transform.SetPosition(startPosition_);
    moveDirection_ = {};
    roundState_ = RoundState::Playing;
    collision_->Reset(); // 前回のEnter/Stay/Exit判定用の履歴を消す。
    // F2では同じ迷路を使い、敵だけランダムに再配置する。
    if (enemy_) enemy_->Reset(startPosition_);
    // 開始地点の開いている通路を向く。遠くの敵へカメラを向ける処理はしない。
    const float yaw = grid_.At(2, 1) != '#' ? 90.0f : 0.0f;
    cameraControl_.SetAngles(yaw, 0.0f);
    cameraControl_.Apply(*world_, player_, camera_);
    // 敵のFind状態・再取得タイマー・経路・IdleもResetで初期化済み。
}

void Stage1Scene::OnUpdate(SceneContext& context, const SceneInput& input, double seconds)
{
    if (input.cancel) { context.scenes.RequestQuit(); return; }
    if (roundState_ != RoundState::Playing)
    {
        // 行き先を指定していない場合だけ、この場で決定を待つ。
        if (input.confirm) context.scenes.RequestQuit();
        return;
    }
    // F2はプレイ中の確認用として残す。GameOverからの自動リトライはしない。
    if (input.alternate) { ResetRound(); return; }

    const float dt = static_cast<float>((std::min)(seconds, 0.1));
    // 矢印キーだけで視点を回す。
    cameraControl_.Rotate(
        input.turnX * 90.0f * dt,
        input.turnY * 90.0f * dt);
    const auto forward = cameraControl_.ForwardXZ();
    const auto right = cameraControl_.RightXZ();
    moveDirection_ = {
        forward.x * input.moveY + right.x * input.moveX,
        0,
        forward.z * input.moveY + right.z * input.moveX
    };
    cameraControl_.Apply(*world_, player_, camera_);
}

void Stage1Scene::OnFixedUpdate(SceneContext& context, double seconds)
{
    if (roundState_ != RoundState::Playing) return;
    motor_.Move(*collision_, player_, moveDirection_, seconds);
    if (enemy_)
        enemy_->Update(seconds, world_->Find(player_)->transform.GetPosition());
    cameraControl_.Apply(*world_, player_, camera_);

    bool reachedGoal = false;
    bool caught = false;
    const ObjectId enemyId = enemy_ ? enemy_->GetObjectId() : ObjectId{};
    for (const auto& event : collision_->Step())
    {
        if (event.phase != TriggerPhase::Enter) continue;

        // firstが必ずプレイヤーとは限らない。並び順の両方を調べる。
        reachedGoal |=
            (event.first == player_ && event.second == goal_) ||
            (event.first == goal_ && event.second == player_);
        caught |= enemy_ && (
            (event.first == player_ && event.second == enemyId) ||
            (event.first == enemyId && event.second == player_));
    }

    if (!reachedGoal && !caught) return;
    // 同じ固定更新で両方へ触れたらゴール優先。イベントの並び順で勝敗を変えない。
    roundState_ = reachedGoal ? RoundState::Clear : RoundState::GameOver;
    moveDirection_ = {};
    const auto& destination = reachedGoal ? settings_.nextScene : settings_.gameOverScene;
    // これは切替予約。今動いているSceneを、この関数の途中で破棄するわけではない。
    if (!destination.empty()) context.scenes.RequestChange(destination);
}

void Stage1Scene::OnExit(SceneContext&) noexcept
{
    enemy_.reset();
    collision_.reset();
    world_.reset();
    grid_ = {};
    player_ = {};
    goal_ = {};
    startPosition_ = {};
    moveDirection_ = {};
    roundState_ = RoundState::Playing;
}

ScenePresentation Stage1Scene::GetPresentation() const
{
    ScenePresentation view;
    view.clearColor = { 0.02f, 0.02f, 0.03f, 1 };
    if (roundState_ == RoundState::Clear)
        view.caption = L"CLEAR | Enter: Quit / Esc: Quit";
    else if (roundState_ == RoundState::GameOver)
        view.caption = L"GAME OVER | Enter: Quit / Esc: Quit";
    else
        view.caption = L"Maze | WASD: Move / Arrows : Look / F2: Retry / Esc: Quit";
    if (enemy_ && roundState_ == RoundState::Playing)
    {
        view.caption += enemy_->State() == EnemyState::Chase ? L" | Enemy: CHASE" : L" | Enemy: FIND";
        view.caption += enemy_->IsThinking() ? L" / Thinking" : enemy_->IsMoving() ? L" / Walking" : L" / Idle";
    }
    view.world = world_.get();
    view.camera = &camera_;
    return view;
}
