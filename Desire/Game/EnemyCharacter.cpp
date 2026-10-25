#include "Game/EnemyCharacter.h"
#include "Game/CollisionCategories.h"
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
    std::size_t FindClip(const ModelAsset& model, std::string_view shortName)
    {
        std::optional<std::size_t> found;
        const auto clips = model.Clips();
        for (std::size_t i = 0; i < clips.size(); ++i)
        {
            std::string_view name = clips[i].name;
            const auto separator = name.find_last_of('|');
            if (separator != std::string_view::npos) name.remove_prefix(separator + 1);
            if (name != shortName) continue;
            if (found) throw std::runtime_error("Duplicate short animation name: " + std::string(shortName));
            found = i;
        }
        if (!found) throw std::runtime_error("Missing enemy animation: " + std::string(shortName));
        return *found;
    }
}

EnemyCharacter::EnemyCharacter(World& world, ModelHandle model, const MapGrid& map,
    float cellSize, const DirectX::XMFLOAT3& playerPosition)
    : world_(world), navigation_(map, cellSize), brain_(navigation_, EnemyTuning::settings)
{
    if (!model) throw std::invalid_argument("EnemyCharacter needs a loaded model.");
    idleClip_ = FindClip(*model, "Idle");
    walkingClip_ = FindClip(*model, "Walking");
    floorOffset_ = model->Bounds().max.y;
    brain_.Reset(playerPosition);

    ObjectDesc desc;
    desc.name = "PoteraEnemy";
    // 初期向きは270度。歩き出したらBrainが進行方向へ向ける。
    // Zの180度は、今回のFBXの上下補正として残す。
    desc.transform.SetEulerDegrees({ 0, brain_.YawDegrees(), 180 });
    const auto p = brain_.Position();
    desc.transform.SetPosition({p.x, p.y + floorOffset_, p.z});
    // 胴体へ接触したら負け。髪や腕まで丸ごと囲まず、小さな球を1個付ける。
    // このモデルはZを180度回しているので、ローカルの-Yがワールドの上になる。
    // 半径0.4m、中心は足元から約0.75m。Sphereなので向きが変わっても半径は同じ。
    desc.collider = Collider::Sphere(0.4f, { 0, -0.75f, 0 }, ColliderMode::Trigger);
    desc.collider->category = GameCollision::Enemy;
    desc.collider->mask = GameCollision::Player;

    instance_ = std::make_unique<ModelInstance>(world_, std::move(model), desc);
    for (auto id : instance_->Parts())
    {
        auto* object = world_.Find(id);
        if (!object || !object->meshRenderer) continue;
        object->meshRenderer->material.surface = SurfaceMode::AlphaClip;
        object->meshRenderer->material.lit = false;
    }
    instance_->Animation().Play(idleClip_, true);
    instance_->Update(world_, 0);
}

void EnemyCharacter::ApplyTransform()
{
    auto* root = world_.Find(instance_->Root());
    if (!root) throw std::logic_error("Enemy root is missing.");
    const auto p = brain_.Position();
    root->transform.SetPosition({p.x, p.y + floorOffset_, p.z});
    root->transform.SetEulerDegrees({0, brain_.YawDegrees(), 180});
}

void EnemyCharacter::Update(double seconds, const DirectX::XMFLOAT3& playerPosition)
{
    brain_.Update(seconds, playerPosition);
    ApplyTransform();
    const bool walk = brain_.IsMoving();
    if (walk != playingWalk_)
    {
        playingWalk_ = walk;
        // 再生を切り替える瞬間だけPlay。毎フレーム先頭へ戻さない。
        instance_->Animation().Play(walk ? walkingClip_ : idleClip_, true);
    }
    instance_->Update(world_, seconds);
}

void EnemyCharacter::Reset(const DirectX::XMFLOAT3& playerPosition)
{
    brain_.Reset(playerPosition);
    ApplyTransform();
    playingWalk_ = false;
    instance_->Animation().Play(idleClip_, true);
    instance_->Update(world_, 0);
}
