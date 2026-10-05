#pragma once
// キャラ1体・Mesh1個ぶんの現在姿勢。モデルの共有データと混ぜず、Animatorが更新する。
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <memory>
inline constexpr std::size_t MaxSkinJoints = 128;
struct SkinMatrix
{
    DirectX::XMFLOAT4X4 position;
    DirectX::XMFLOAT4X4 normal;
};
struct SkinPose
{
    // 未使用の欄もゼロ初期化。使うのはcount個だけ。GPUへは全体を256B境界で渡す。
    std::array<SkinMatrix, MaxSkinJoints> matrices{};
    std::size_t count = 0;
};
using SkinPoseHandle = std::shared_ptr<const SkinPose>;
