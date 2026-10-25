// ひな形の登録とコピー。ここでGPUを作ったり、GameObjectを直接newしたりはしない。
#include "World/ObjectFactory.h"
#include "World/World.h"
#include <stdexcept>
#include <utility>

void ObjectFactory::Register(std::string prefabName, ObjectDesc defaults)
{
    if (prefabName.empty() || defaults.name.empty())
        throw std::invalid_argument("Prefab and object names must not be empty.");
    if (prefabs_.contains(prefabName))
        throw std::invalid_argument("Duplicate prefab: " + prefabName);

    // 入口で検査。壊れたひな形を登録して、100個並べてから気付くのはつらい。
    (void)defaults.transform.MakeWorldMatrix();
    if (defaults.meshRenderer) defaults.meshRenderer->material.Validate();
    if (defaults.collider) (void)defaults.collider->ToWorld(defaults.transform,
        defaults.meshRenderer&&defaults.meshRenderer->mesh?&defaults.meshRenderer->mesh->Bounds():nullptr);

    // 引数は値で受け取ったので、呼び出し元のObjectDescを後で変更しても影響しない。
    prefabs_.emplace(std::move(prefabName), std::move(defaults));
}

ObjectDesc ObjectFactory::MakeDescription(std::string_view prefabName) const
{
    const auto found = prefabs_.find(prefabName);
    if (found == prefabs_.end())
        throw std::invalid_argument("Unknown prefab: " + std::string(prefabName));

    // 参照ではなく値で返す。受け取った側が自由に調整できる注文書になる。
    return found->second;
}

ObjectId ObjectFactory::Spawn(World& world, std::string_view prefabName,
    const DirectX::XMFLOAT3& position) const
{
    auto desc = MakeDescription(prefabName);
    desc.transform.SetPosition(position);

    // 本体の確保は既存Worldへ。SceneArenaのObjectPoolをそのまま使う。
    return world.Spawn(desc);
}

bool ObjectFactory::Contains(std::string_view prefabName) const
{
    return prefabs_.contains(prefabName);
}

bool ObjectFactory::Remove(std::string_view prefabName)
{
    const auto found = prefabs_.find(prefabName);
    if (found == prefabs_.end()) return false;

    // 消すのは登録だけ。既にWorldへ置いた物は自分のデータと共有所有を持っている。
    prefabs_.erase(found);
    return true;
}

void ObjectFactory::Clear() noexcept
{
    // ひな形側の共有所有も外す。画像や形状を登録したまま常駐させないための出口。
    prefabs_.clear();
}
