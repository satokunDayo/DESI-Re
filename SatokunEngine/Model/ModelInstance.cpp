#include "Model/ModelInstance.h"
#include <algorithm>
#include <stdexcept>

ModelInstance::ModelInstance(World& world, ModelHandle model, ObjectDesc root)
    : animation_(std::move(model))
{
    if(root.meshRenderer) throw std::invalid_argument("Model root must not have a MeshRenderer.");
    if(root.collider && root.collider->autoFit)
    {
        // ボーンごとの判定ではなく、インポート時の全体Boundsに固定した判定を作る。
        Transform identity;
        const auto fit=root.collider->ToWorld(identity,&animation_.Model()->Bounds());
        root.collider->autoFit=false; root.collider->center=fit.center;
        if(root.collider->shape==ColliderShape::Aabb) root.collider->halfExtents=fit.halfExtents;
        else root.collider->radius=fit.radius;
    }
    std::vector<ObjectDesc> descriptions{root};
    const auto modelParts=animation_.Model()->Parts();
    for(std::size_t i=0;i<modelParts.size();++i)
    {
        ObjectDesc desc; desc.name=root.name+"/"+modelParts[i].name;
        desc.transform=root.transform; desc.placementLayer=root.placementLayer;
        desc.meshRenderer=MeshRenderer{modelParts[i].mesh,modelParts[i].material};
        desc.meshRenderer->skinPose=animation_.Pose(i);
        descriptions.push_back(std::move(desc));
    }
    // 途中で満杯になっても、今回のキャラだけ全部取り消す。半身だけ残さない。
    auto ids=world.SpawnBatch(descriptions);
    root_=ids.front(); ids.erase(ids.begin()); parts_=std::move(ids);
}
bool ModelInstance::Update(World& world, double seconds)
{
    const auto* root=world.Find(root_);
    if(!root) { RequestDestroy(world); return false; }
    animation_.Update(seconds);
    for(auto id:parts_) if(auto* part=world.Find(id)) part->transform=root->transform;
    return true;
}
void ModelInstance::SetVisible(World& world, bool visible)
{
    for(auto id:parts_) if(auto* part=world.Find(id))
        if(part->meshRenderer) part->meshRenderer->render.visible=visible;
}
void ModelInstance::RequestDestroy(World& world) noexcept
{
    world.RequestDestroy(root_);
    for(auto id:parts_) world.RequestDestroy(id);
}
