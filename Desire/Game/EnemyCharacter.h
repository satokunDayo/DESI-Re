#pragma once
#include "Game/AI/MazeEnemyBrain.h"
#include "Model/ModelInstance.h"
#include <memory>

// 敵1体の窓口。Brainの判断を、Worldの位置とFBXアニメに反映する。
class EnemyCharacter final
{
public:
    EnemyCharacter(World& world, ModelHandle model, const MapGrid& map,
        float cellSize, const DirectX::XMFLOAT3& playerPosition);
    EnemyCharacter(const EnemyCharacter&) = delete;
    EnemyCharacter& operator=(const EnemyCharacter&) = delete;
    void Update(double seconds, const DirectX::XMFLOAT3& playerPosition);
    // 地図・モデルを読み直さず、敵のランダム再配置とAI初期化を行う。
    void Reset(const DirectX::XMFLOAT3& playerPosition);
    ObjectId GetObjectId() const noexcept { return instance_->Root(); }
    EnemyState State() const noexcept { return brain_.State(); }
    bool IsMoving() const noexcept { return brain_.IsMoving(); }
    bool IsThinking() const noexcept { return brain_.IsThinking(); }

private:
    void ApplyTransform();
    World& world_;
    MazeNavigation navigation_; // mapはシーンから借りる。
    MazeEnemyBrain brain_;
    std::unique_ptr<ModelInstance> instance_;
    std::size_t idleClip_ = 0, walkingClip_ = 0;
    bool playingWalk_ = false;
    float floorOffset_ = 0;
};
