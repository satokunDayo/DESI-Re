// 拡大→回転→移動を行列1個にまとめる。親子Transformや前フレームとの補間はまだない。
#include "Transform.h"
#include <cmath>
#include <stdexcept>

namespace
{
    // NaNや無限大を入口で止める。1つ混ざると行列全体へ広がって画面から消えかねない。
    void Validate(const DirectX::XMFLOAT3& v)
    {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            throw std::invalid_argument("Transform requires finite values.");
    }
}

// 検査してから代入するので、失敗したら元の位置が残る。
void Transform::SetPosition(const DirectX::XMFLOAT3& value)
{
    Validate(value); position_ = value;
}
// 負の倍率による反転や、ゼロで潰す配置は今は受け付けない。
void Transform::SetScale(const DirectX::XMFLOAT3& value)
{
    Validate(value);
    if (value.x <= 0 || value.y <= 0 || value.z <= 0)
        throw std::invalid_argument("Scale must be positive. Use visibility to hide an object.");
    scale_ = value;
}
// 何周もした角度を360度周期へ戻し、ラジアンへ換算して回転を作る。
void Transform::SetEulerDegrees(const DirectX::XMFLOAT3& value)
{
    using namespace DirectX;
    Validate(value);
    const auto radians = [](float degrees) { return XMConvertToRadians(std::remainder(degrees, 360.0f)); };
    // 計算用のSIMD型から保存用のXMFLOAT4へ戻す。正規化して回転の長さをそろえる。
    XMStoreFloat4(&rotation_, XMQuaternionNormalize(XMQuaternionRotationRollPitchYaw(
        radians(value.x), radians(value.y), radians(value.z))));
}
Transform Transform::TransformedBy(const Transform& parent) const
{
    using namespace DirectX;
    const auto& p = parent.scale_;
    if (p.x != p.y || p.y != p.z)
        throw std::invalid_argument("Parent scale must be uniform; shear is not supported.");
    const auto parentMatrix = parent.MakeWorldMatrix();
    Transform result;
    XMFLOAT3 position;
    XMStoreFloat3(&position, XMVector3TransformCoord(XMLoadFloat3(&position_), XMLoadFloat4x4(&parentMatrix)));
    result.SetPosition(position);
    result.SetScale({scale_.x*p.x, scale_.y*p.x, scale_.z*p.x});
    // DirectXMathのMultiplyは、先の回転→後の回転の合成を返す。
    XMStoreFloat4(&result.rotation_, XMQuaternionNormalize(
        XMQuaternionMultiply(XMLoadFloat4(&rotation_), XMLoadFloat4(&parent.rotation_))));
    (void)result.MakeWorldMatrix();
    return result;
}
// 行ベクトルで頂点×S×R×Tの順。掛ける順を変えると、その場で回転せず公転したりする。
DirectX::XMFLOAT4X4 Transform::MakeWorldMatrix() const
{
    using namespace DirectX;
    const auto matrix = XMMatrixScaling(scale_.x, scale_.y, scale_.z) *
        XMMatrixRotationQuaternion(XMLoadFloat4(&rotation_)) *
        XMMatrixTranslation(position_.x, position_.y, position_.z);
    // 入力が有限でも極端な値の計算であふれることはある。出来上がりも確認する。
    if (XMMatrixIsNaN(matrix) || XMMatrixIsInfinite(matrix))
        throw std::overflow_error("Transform matrix overflow.");
    XMFLOAT4X4 result;
    // 返すのは保存用の値。内部の一時変数への参照を返さない。
    XMStoreFloat4x4(&result, matrix);
    return result;
}
