#pragma once
// 物の位置・回転・大きさ。動かし方や衝突は持たず、今どこにあるかを表す。
#include <DirectXMath.h>

// 1個の物の配置。初版は親子関係なし。角度の入力は度、内部はQuaternion。
class Transform final
{
public:
    // ワールド内の位置を保存する。タイル座標から変換するなら呼ぶ側で変換して渡す。
    void SetPosition(const DirectX::XMFLOAT3& value);
    // 各軸の倍率。今は正数だけで、非表示をscale=0で表現しない。
    void SetScale(const DirectX::XMFLOAT3& value);
    // 入力はX/Y/Z軸まわりの度数。保存時にQuaternionへ変換する。
    void SetEulerDegrees(const DirectX::XMFLOAT3& value);
    // 位置と倍率は読み取り参照で渡す。値の変更は検査付きのSet関数を通す。
    const DirectX::XMFLOAT3& GetPosition() const noexcept { return position_; }
    const DirectX::XMFLOAT3& GetScale() const noexcept { return scale_; }
    // 頂点のローカル座標をワールド座標へ変える4×4行列を、その都度作る。
    DirectX::XMFLOAT4X4 MakeWorldMatrix() const;
    // 自分の配置を親の空間へ移す。親の倍率は均一だけ。親子関係の保持・自動追従はしない。
    // 地図1層をまとめて回しても、各タイルの向きと原型の倍率を失わないための合成。
    Transform TransformedBy(const Transform& parent) const;
private:
    DirectX::XMFLOAT3 position_{ 0, 0, 0 };
    DirectX::XMFLOAT3 scale_{ 1, 1, 1 };
    // 回転なしは(0,0,0,1)。4つの数を角度4個だと思って直接いじらない。
    DirectX::XMFLOAT4 rotation_{ 0, 0, 0, 1 };
};
