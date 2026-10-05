#pragma once
// 配置する物の共通データ。継承で種類を増やす前に、必要なデータを組み合わせる形にしている。
#include "World/Transform.h"
#include "Physics/Collider.h"
#include "Render/RenderSettings.h"
#include "Render/Material.h"
#include "Render/GpuMesh.h"
#include "Model/SkinPose.h"
#include <optional>
#include <string>

// ポインタの代わりに覚えておく識別子。World・席番号・世代の3つで取り違えを防ぐ。
struct ObjectId
{
    std::uint64_t world=0;
    std::uint32_t index=0;
    // 席を使い回しても世代が変わる。昔の敵IDで新しい家を触ってしまう事故を防ぐ。
    std::uint64_t generation=0;
    bool operator==(const ObjectId&) const=default;
};
// データを合成して機能を持たせる。描画しないオブジェクトなら省略できる。
// 形状・材質・描画順をまとめたデータ。これ自体がDX12命令を発行するクラスではない。
struct MeshRenderer
{
    GpuMeshHandle mesh;
    Material material;
    RenderSettings render;
    // キャラごとの現在姿勢。Meshは共有しても、この配列は別々に持つ。
    SkinPoseHandle skinPose;
};
// Spawnへ渡す注文書。GameObjectは内容をコピーするが、MeshとTextureの実体は共有する。
struct ObjectDesc
{
    std::string name="Object";
    Transform transform;
    // 描画しない物なら空にできる。空にしただけでTriggerやColliderが付くわけではない。
    std::optional<MeshRenderer> meshRenderer;
    int placementLayer=0; // 配置時の分類。描画順や物理的な高さではない。
    std::optional<Collider> collider; // 描画なしのTriggerも、MeshとセットのSolidも同じ入口。
};
// 名前と配置、任意の描画情報を持つ。汎用Component登録やAIの更新はまだこの型に入っていない。
class GameObject final
{
public:
    // 注文書から初期状態を作る。通常はWorld::Spawn→ObjectPool::Rentを通って呼ばれる。
    explicit GameObject(const ObjectDesc& desc):transform(desc.transform),meshRenderer(desc.meshRenderer),
        placementLayer(desc.placementLayer),collider(desc.collider),name_(desc.name){}
    Transform transform;
    std::optional<MeshRenderer> meshRenderer;
    int placementLayer=0;
    std::optional<Collider> collider;
    // 名前は読み取りで借りる。IDではないので、同名オブジェクトがいても識別はObjectIdで行う。
    const std::string& GetName() const noexcept{return name_;}
private:
    std::string name_;
};
// そのフレームの描画依頼。位置を行列へ変換済みで、GPU資産の共有所有もコピーする。
struct alignas(16) RenderItem
{
    DirectX::XMFLOAT4X4 world{};
    MeshRenderer renderer;
    // 同じ描画レイヤー・順番なら生成順で決めるための番号。
    std::uint64_t spawnOrder=0;
};
