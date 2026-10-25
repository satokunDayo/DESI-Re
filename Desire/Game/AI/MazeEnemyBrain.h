#pragma once
#include "Game/AI/EnemySettings.h"
#include "Game/AI/MazeNavigation.h"
#include <cstdint>
#include <optional>

enum class EnemyState { Find, Chase };

// 判断と経路に沿った移動だけを担当する。FBXやWorldには触れない。
class MazeEnemyBrain final
{
public:
    MazeEnemyBrain(const MazeNavigation& navigation, const EnemyAiSettings& settings,
        std::uint32_t seed = std::random_device{}());
    // 通常の開始・リトライ。プレイヤーから離れた通路へランダム配置する。
    void Reset(const DirectX::XMFLOAT3& playerPosition);
    // 指定マスから始める入口。固定配置や動作確認にも使える。
    void ResetAt(MazeCell cell);
    void Update(double seconds, const DirectX::XMFLOAT3& playerPosition);
    bool CanSeePlayer(const DirectX::XMFLOAT3& playerPosition) const;

    EnemyState State() const noexcept { return state_; }
    const DirectX::XMFLOAT3& Position() const noexcept { return position_; }
    float YawDegrees() const noexcept { return yawDegrees_; }
    bool IsMoving() const noexcept { return moving_; }
    bool IsThinking() const noexcept { return thinkRemaining_ > 0; }
    bool SeesPlayer() const noexcept { return seesPlayer_; }
    std::uint64_t SampleCount() const noexcept { return sampleCount_; }
    const DirectX::XMFLOAT3& LastEstimate() const noexcept { return lastEstimate_; }

private:
    void ChangeState(EnemyState state);
    void RefreshRoute(const DirectX::XMFLOAT3& playerPosition);
    void MoveAlongRoute(double seconds);
    const MazeNavigation& navigation_;
    const EnemyAiSettings& settings_;
    std::mt19937 random_;
    EnemyState state_ = EnemyState::Find;
    DirectX::XMFLOAT3 position_{}, lastEstimate_{};
    float yawDegrees_ = 270;
    double untilSample_ = 0, thinkRemaining_ = 0, unseenSeconds_ = 0;
    bool moving_ = false, seesPlayer_ = false;
    std::uint64_t sampleCount_ = 0;
    std::vector<MazeCell> route_;
    std::size_t routeIndex_ = 0;
    // 経路の引き直し中も、移動途中の1辺は先のマス中心まで守る。
    // 途中位置から新しい角へ斜めに飛ぶと壁を横切ってしまうため。
    std::optional<MazeCell> waypoint_;
};
