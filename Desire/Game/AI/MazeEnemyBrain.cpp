#include "Game/AI/MazeEnemyBrain.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

MazeEnemyBrain::MazeEnemyBrain(const MazeNavigation& navigation,
    const EnemyAiSettings& settings, std::uint32_t seed)
    : navigation_(navigation), settings_(settings), random_(seed)
{ settings_.Validate(); }

void MazeEnemyBrain::Reset(const DirectX::XMFLOAT3& playerPosition)
{
    settings_.Validate();
    ResetAt(navigation_.RandomSpawn(navigation_.ToCell(playerPosition), settings_.spawnMinPathCells, random_));
}

void MazeEnemyBrain::ResetAt(MazeCell cell)
{
    settings_.Validate();
    if (!navigation_.Walkable(cell)) throw std::invalid_argument("Enemy spawn is inside a wall.");
    position_ = navigation_.Position(cell);
    lastEstimate_ = position_;
    yawDegrees_ = settings_.initialYawDegrees;
    state_ = EnemyState::Find;
    untilSample_ = thinkRemaining_ = unseenSeconds_ = 0;
    moving_ = seesPlayer_ = false;
    sampleCount_ = 0;
    route_.clear(); routeIndex_ = 0; waypoint_.reset();
}

bool MazeEnemyBrain::CanSeePlayer(const DirectX::XMFLOAT3& player) const
{
    if (!std::isfinite(player.x) || !std::isfinite(player.y) || !std::isfinite(player.z))
        throw std::invalid_argument("Player position must be finite.");
    // 敵の目とプレイヤーの胸を、地面から1mの点として見る。
    // 両方に同じ高さを足すので、差分では相殺される。
    const double dx = double(player.x) - position_.x;
    const double dy = double(player.y) - position_.y;
    const double dz = double(player.z) - position_.z;
    const double distanceSquared = dx*dx + dy*dy + dz*dz;
    if (distanceSquared > double(settings_.sightDistance) * settings_.sightDistance) return false;
    if (distanceSquared > 1e-12)
    {
        const double yaw = yawDegrees_ * std::numbers::pi / 180;
        const double cosine = (std::sin(yaw)*dx + std::cos(yaw)*dz) / std::sqrt(distanceSquared);
        const double minimum = std::cos(settings_.sightAngleDegrees * std::numbers::pi / 360);
        if (cosine < minimum) return false;
    }
    return navigation_.HasLineOfSight(position_, player);
}

void MazeEnemyBrain::ChangeState(EnemyState state)
{
    if (state_ == state) return;
    state_ = state;
    untilSample_ = 0; // 発見・見失いの切り替わりでは、次の周期を待たず更新する。
    unseenSeconds_ = 0;
    if (state_ == EnemyState::Chase) thinkRemaining_ = 0;
}

void MazeEnemyBrain::RefreshRoute(const DirectX::XMFLOAT3& player)
{
    const bool chase = state_ == EnemyState::Chase;
    const float errorRadius = settings_.findErrorRadius * (chase ? settings_.chaseErrorScale : 1.0f);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double angle = unit(random_) * 2 * std::numbers::pi;
    const double radius = std::sqrt(unit(random_)) * errorRadius;
    lastEstimate_ = {
        player.x + static_cast<float>(std::cos(angle) * radius),
        player.y,
        player.z + static_cast<float>(std::sin(angle) * radius)
    };

    // 移動中なら先のマスを経路の出発点にする。
    // waypoint_自体を変えないので、今いる通路の途中から角を斜めに抜けない。
    const auto anchor = waypoint_.value_or(navigation_.ToCell(position_));
    route_ = navigation_.RouteToEstimate(anchor, lastEstimate_);
    routeIndex_ = 0;
    ++sampleCount_;
    untilSample_ = settings_.findIntervalSeconds * (chase ? settings_.chaseIntervalScale : 1.0);
    if (!chase) thinkRemaining_ = settings_.findThinkSeconds;
}

void MazeEnemyBrain::MoveAlongRoute(double seconds)
{
    double remaining = settings_.moveSpeed * seconds;
    while (remaining > 0)
    {
        if (!waypoint_)
        {
            if (routeIndex_ == route_.size()) break;
            waypoint_ = route_[routeIndex_++];
        }
        const auto target = navigation_.Position(*waypoint_);
        const double dx = double(target.x) - position_.x, dz = double(target.z) - position_.z;
        const double distance = std::hypot(dx, dz);
        if (distance < 1e-6) { position_ = target; waypoint_.reset(); continue; }
        yawDegrees_ = static_cast<float>(std::atan2(dx, dz) * 180 / std::numbers::pi);
        const double step = (std::min)(distance, remaining);
        if (step >= distance)
        { position_ = target; waypoint_.reset(); }
        else
        {
            position_.x += static_cast<float>(dx / distance * step);
            position_.z += static_cast<float>(dz / distance * step);
        }
        remaining -= step;
        moving_ = true;
    }
}

void MazeEnemyBrain::Update(double seconds, const DirectX::XMFLOAT3& playerPosition)
{
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Enemy delta must be finite and nonnegative.");
    settings_.Validate();
    moving_ = false;

    // 本当の位置は視界に入ったかの判定に使う。
    // 経路の目的地には、取得周期ごとの誤差付き座標だけを渡す。
    seesPlayer_ = CanSeePlayer(playerPosition);
    if (seesPlayer_)
    {
        unseenSeconds_ = 0;
        ChangeState(EnemyState::Chase);
    }
    else if (state_ == EnemyState::Chase)
    {
        unseenSeconds_ += seconds;
        if (unseenSeconds_ >= settings_.loseSightSeconds) ChangeState(EnemyState::Find);
    }

    untilSample_ -= seconds;
    if (untilSample_ <= 0) RefreshRoute(playerPosition);

    // Findの「考える」は敵だけの待機時間。ゲーム全体を止めない。
    const double walkingSeconds = (std::max)(0.0, seconds - thinkRemaining_);
    thinkRemaining_ = (std::max)(0.0, thinkRemaining_ - seconds);
    MoveAlongRoute(walkingSeconds);
}
