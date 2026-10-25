// 視点を基準に座標を変え、画面へ投影する計算。移動入力を読む処理はここにはない。
#include "Camera/Camera.h"
#include <cmath>
#include <stdexcept>

namespace
{
    // near=0やfar<=nearは投影を壊すので拒否。平行投影にもこのエンジンでは同じ規則を使う。
    void ValidateRange(float nearZ, float farZ)
    {
        if (!std::isfinite(nearZ) || !std::isfinite(farZ) || nearZ <= 0 || farZ <= nearZ)
            throw std::invalid_argument("Camera requires 0 < near < far.");
    }
}
// 位置と注視点が同じでは向きを決められない。上方向も視線と平行なら横を決められない。
void Camera::LookAt(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& target,
    const DirectX::XMFLOAT3& up)
{
    using namespace DirectX;
    for (const auto& v : { position, target, up })
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            throw std::invalid_argument("Camera vectors must be finite.");
    const auto forward = XMLoadFloat3(&target) - XMLoadFloat3(&position);
    const auto upVector = XMLoadFloat3(&up);
    // 外積で視線と上方向から横方向を作る。ほぼゼロならカメラの姿勢が成立していない。
    const auto right = XMVector3Cross(forward, upVector);
    if (XMVectorGetX(XMVector3LengthSq(forward)) < 1e-8f ||
        XMVectorGetX(XMVector3LengthSq(right)) < 1e-8f)
        throw std::invalid_argument("Camera direction/up is degenerate.");
    // 新しい値で正常な行列になるか確認してから、カメラの設定を保存する。
    const auto view = XMMatrixLookAtLH(XMLoadFloat3(&position), XMLoadFloat3(&target), upVector);
    if (XMMatrixIsNaN(view) || XMMatrixIsInfinite(view))
        throw std::invalid_argument("Camera view overflow.");
    position_ = position; target_ = target; up_ = up;
}
// 視野角が狭すぎる/180度へ近すぎる設定は避け、有限な範囲だけ通す。
void Camera::SetPerspective(float fov, float nearZ, float farZ)
{
    ValidateRange(nearZ, farZ);
    if (!std::isfinite(fov) || fov <= 0 || fov >= 179)
        throw std::invalid_argument("Perspective FOV must be in (0, 179) degrees.");
    orthographic_ = false; fovDegrees_ = fov; nearZ_ = nearZ; farZ_ = farZ;
}
// 縦に見える範囲を保存。横の範囲は描画時のaspectを掛けて求める。
void Camera::SetOrthographic(float height, float nearZ, float farZ)
{
    ValidateRange(nearZ, farZ);
    if (!std::isfinite(height) || height <= 0)
        throw std::invalid_argument("Orthographic size must be positive.");
    orthographic_ = true; verticalSize_ = height; nearZ_ = nearZ; farZ_ = farZ;
}
// WorldRendererはこの行列を1回作り、各物体のWorld行列と組み合わせる。
DirectX::XMFLOAT4X4 Camera::MakeViewProjection(float aspect) const
{
    using namespace DirectX;
    if (!std::isfinite(aspect) || aspect <= 0) throw std::invalid_argument("Invalid camera aspect.");
    const auto view = XMMatrixLookAtLH(XMLoadFloat3(&position_), XMLoadFloat3(&target_), XMLoadFloat3(&up_));
    // 透視か平行かはこの分岐。描く形状が画像か立体かとは独立して選べる。
    const auto projection = orthographic_
        ? XMMatrixOrthographicLH(verticalSize_ * aspect, verticalSize_, nearZ_, farZ_)
        : XMMatrixPerspectiveFovLH(XMConvertToRadians(fovDegrees_), aspect, nearZ_, farZ_);
    // 頂点×World×View×Projectionという順序の、後半2つをまとめて返す。
    const auto matrix = view * projection;
    if (XMMatrixIsNaN(matrix) || XMMatrixIsInfinite(matrix))
        throw std::overflow_error("Camera matrix overflow.");
    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, matrix);
    return result;
}
