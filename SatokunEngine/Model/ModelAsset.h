#pragma once
// FBXから取り出した共有資産。何体置いてもMesh・画像・アニメの元データは共有する。
#include "Render/GpuMesh.h"
#include "Render/Material.h"
#include <span>
#include <string>
#include <vector>
struct ModelData;
struct AnimationClipInfo { std::string name; double duration = 0; };
struct ModelPart
{
    std::string name;
    GpuMeshHandle mesh;
    Material material;
};
class ModelAsset final
{
public:
    std::span<const ModelPart> Parts() const noexcept { return parts_; }
    std::span<const AnimationClipInfo> Clips() const noexcept { return clips_; }
    const MeshBounds& Bounds() const noexcept { return bounds_; }
    std::size_t NodeCount() const noexcept;
    // ufbxが保持する解析結果とベイク済みキーの量。CPU Arenaの統計とは別。
    std::size_t ImportMemoryBytes() const noexcept;
private:
    friend class ModelLoader;
    friend class Animator;
    std::shared_ptr<ModelData> data_;
    std::vector<ModelPart> parts_;
    std::vector<AnimationClipInfo> clips_;
    MeshBounds bounds_;
};
using ModelHandle = std::shared_ptr<const ModelAsset>;
