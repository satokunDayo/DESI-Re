#pragma once
// FBX形式の事情はこの裏側へ閉じ込める。ゲーム側はModelAssetとAnimatorだけ見ればよい。
#include "Model/ModelAsset.h"
#include "ThirdParty/ufbx/ufbx.h"
#include <memory>
using FbxScenePtr = std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)>;
using BakedAnimationPtr = std::unique_ptr<ufbx_baked_anim, decltype(&ufbx_free_baked_anim)>;
struct ModelBinding
{
    std::uint32_t node = 0;
    DirectX::XMFLOAT4X4 geometryToBone{};
};
struct ModelSkin
{
    DirectX::XMFLOAT4X4 inverseGeometry{};
    std::vector<ModelBinding> bindings;
};
struct ModelData
{
    FbxScenePtr scene{nullptr,ufbx_free_scene};
    std::vector<BakedAnimationPtr> animations;
    std::vector<ModelSkin> skins; // Partsと同じ順番。最後の関節はウェイトなし頂点用。
    std::vector<std::uint32_t> nodeOrder; // 親→子の順。配列の番号そのものが親子順とは限らない。
    std::size_t memoryBytes = 0;
};
// ufbxは列ベクトル、DirectXMathは行ベクトル。転置して同じ変換を表す。
inline DirectX::XMFLOAT4X4 ModelMatrix(const ufbx_matrix& m)
{
    return {float(m.m00),float(m.m10),float(m.m20),0,
        float(m.m01),float(m.m11),float(m.m21),0,
        float(m.m02),float(m.m12),float(m.m22),0,
        float(m.m03),float(m.m13),float(m.m23),1};
}
