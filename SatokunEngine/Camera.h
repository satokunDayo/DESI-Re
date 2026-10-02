#pragma once
// 同じWorldをどう見るかを決めるカメラ。2D/3DでWorldそのものを別物にしなくてよい。
#include <DirectXMath.h>

// Cameraは描き方だけを保持。入力・キャラクター移動は所有しない。
class Camera final
{
public:
    // カメラの位置・注視点・上方向を指定。追従やマウス操作は別の制御から呼ぶ。
    void LookAt(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& target,
        const DirectX::XMFLOAT3& up = { 0, 1, 0 });
    // 遠いものを小さく見せる透視投影。視野角は縦方向の度数。
    void SetPerspective(float verticalFovDegrees, float nearZ, float farZ);
    // 距離で見かけの大きさが変わらない平行投影。verticalSizeは見える高さのワールド単位。
    void SetOrthographic(float verticalSize, float nearZ, float farZ);
    // trueなら奥行きで前後判定。falseならWorldの描画順で重ねる。カメラ全体の設定。
    void SetDepthEnabled(bool enabled) noexcept { depthEnabled_ = enabled; }
    bool IsDepthEnabled() const noexcept { return depthEnabled_; }
    // 実画面の横/縦比を受け取り、視点変換と投影をまとめる。
    DirectX::XMFLOAT4X4 MakeViewProjection(float aspect) const;
private:
    // 既定位置はZ=-10から原点を見る。左手座標系の行列を使う。
    DirectX::XMFLOAT3 position_{ 0, 0, -10 };
    DirectX::XMFLOAT3 target_{ 0, 0, 0 };
    DirectX::XMFLOAT3 up_{ 0, 1, 0 };
    bool orthographic_ = false;
    bool depthEnabled_ = true;
    float fovDegrees_ = 60;
    float verticalSize_ = 10;
    // 描画する近端と遠端。メートル固定ではなく、Worldで決めた距離単位。
    float nearZ_ = 0.1f;
    float farZ_ = 500;
};
