#pragma once
// Cameraは描き方、Controllerは追い方。World内の相手はポインタでなくIDで探す。
#include "Camera/Camera.h"
#include "World/World.h"

class FollowCamera final
{
public:
    DirectX::XMFLOAT3 offset{0,4,-7},lookOffset{0,.5f,0};
    // 対象が消えていたらfalse。削除済みのキャラを追ってメモリを踏まない。
    bool Apply(const World& world,ObjectId target,Camera& camera) const;
};

class FirstPersonCamera final
{
public:
    DirectX::XMFLOAT3 eyeOffset{0,.35f,0};
    void SetAngles(float yawDegrees,float pitchDegrees);
    // マウス差分はフレームに1回。固定更新の回数ぶん掛け直さない。
    void Rotate(float yawDeltaDegrees,float pitchDeltaDegrees);
    DirectX::XMFLOAT3 ForwardXZ() const noexcept;
    DirectX::XMFLOAT3 RightXZ() const noexcept;
    bool Apply(const World& world,ObjectId target,Camera& camera) const;
private:
    float yaw_=0,pitch_=0;
};
