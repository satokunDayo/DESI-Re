#include "CameraControllers.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

bool FollowCamera::Apply(const World& world,ObjectId target,Camera& camera) const
{
    const auto* object=world.Find(target);if(!object)return false;
    const auto p=object->transform.GetPosition();
    camera.LookAt({p.x+offset.x,p.y+offset.y,p.z+offset.z},{p.x+lookOffset.x,p.y+lookOffset.y,p.z+lookOffset.z});
    return true;
}
void FirstPersonCamera::SetAngles(float yaw,float pitch)
{
    if(!std::isfinite(yaw)||!std::isfinite(pitch))throw std::invalid_argument("Camera angles must be finite.");
    yaw_=std::remainder(yaw,360.0f);pitch_=std::clamp(pitch,-85.0f,85.0f);
}
void FirstPersonCamera::Rotate(float yaw,float pitch)
{
    if(!std::isfinite(yaw)||!std::isfinite(pitch))throw std::invalid_argument("Camera delta must be finite.");
    // 足す前にdoubleへ。大きな差分でもfloatの加算でInfinityにしない。
    SetAngles(static_cast<float>(std::remainder(double(yaw_)+yaw,360.0)),
        static_cast<float>(std::clamp(double(pitch_)+pitch,-85.0,85.0)));
}
DirectX::XMFLOAT3 FirstPersonCamera::ForwardXZ() const noexcept
{const float y=DirectX::XMConvertToRadians(yaw_);return {std::sin(y),0,std::cos(y)};}
DirectX::XMFLOAT3 FirstPersonCamera::RightXZ() const noexcept
{const float y=DirectX::XMConvertToRadians(yaw_);return {std::cos(y),0,-std::sin(y)};}
bool FirstPersonCamera::Apply(const World& world,ObjectId target,Camera& camera) const
{
    const auto* object=world.Find(target);if(!object)return false;
    const auto p=object->transform.GetPosition();
    const DirectX::XMFLOAT3 eye{p.x+eyeOffset.x,p.y+eyeOffset.y,p.z+eyeOffset.z};
    const float yaw=DirectX::XMConvertToRadians(yaw_),pitch=DirectX::XMConvertToRadians(pitch_);
    camera.LookAt(eye,{eye.x+std::sin(yaw)*std::cos(pitch),eye.y+std::sin(pitch),eye.z+std::cos(yaw)*std::cos(pitch)});
    return true;
}
