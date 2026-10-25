// オブジェクトの生成・検索・削除予約と描画一覧。現状はメインスレッドで使う。
#include "World/World.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

// 描画前に色・UV・透過方式を検査。公開データを書き換えられるのでSpawn時だけでなく一覧作成時も確認。
void Material::Validate() const
{
    for(float c:{color.x,color.y,color.z,alphaCutoff})
        if(!std::isfinite(c)||c<0||c>1)throw std::invalid_argument("Material color/cutoff must be in [0,1].");
    for(float value:{uvScale.x,uvScale.y,uvOffset.x,uvOffset.y})
        if(!std::isfinite(value))throw std::invalid_argument("Material UV must be finite.");
    if(surface!=SurfaceMode::Opaque&&surface!=SurfaceMode::AlphaClip)throw std::invalid_argument("Unknown surface mode.");
}
namespace
{
    // Worldの通し番号。他WorldのObjectIdを間違って渡しても見つからないようにする。
    std::uint64_t NextWorldId()
    {
        static std::uint64_t next=1;
        if(next==(std::numeric_limits<std::uint64_t>::max)())throw std::overflow_error("World IDs exhausted.");
        return next++;
    }
    // 個数×要素サイズが上限を超えないか先に確認。配列を確保してからあふれに気づいても遅い。
    std::size_t Capacity(std::size_t count)
    {
        if(!count||count>BlockAllocator::MaxAllocationSize/sizeof(RenderItem)||
            count>BlockAllocator::MaxAllocationSize/sizeof(GameObject)||count>(std::numeric_limits<std::uint32_t>::max)())
            throw std::invalid_argument("World capacity out of range.");
        return count;
    }
}
// GameObject用Pool、名簿、RenderItem配列の領域を用意する。
World::World(MemoryManager& memory,std::size_t capacity):id_(NextWorldId()),
    pool_(memory.CreatePool<GameObject>(ArenaType::Scene,Capacity(capacity))),slots_(capacity),
    renderMemory_(memory.Allocate(ArenaType::Scene,capacity*sizeof(RenderItem),alignof(RenderItem)))
{
    if(!renderMemory_)throw std::bad_alloc();
    // MemoryBlockは場所だけなので、ここでRenderItemを構築する。解放前には対応するdestroy_nが必要。
    std::uninitialized_value_construct_n(static_cast<RenderItem*>(renderMemory_.Data()),capacity);
}
// Objectの所有を外し、RenderItemを破棄。その後メンバーのMemoryBlockが領域を返す。
World::~World(){Clear();std::destroy_n(static_cast<RenderItem*>(renderMemory_.Data()),slots_.size());}
// 前の描画依頼が持つMesh/Textureの共有所有も外す。件数だけゼロにすると古い資産が残ってしまう。
void World::InvalidateRenderList() const noexcept
{
    auto* items=static_cast<RenderItem*>(renderMemory_.Data());
    for(std::size_t i=0;i<renderCount_;++i)items[i]=RenderItem{};
    renderCount_=0;
}
// 空いた席を探し、PoolでGameObjectを構築。成功してから名簿へ登録する。
ObjectId World::Spawn(const ObjectDesc& desc)
{
    if(desc.name.empty())throw std::invalid_argument("An object needs a name.");
    if(desc.meshRenderer)desc.meshRenderer->material.Validate();
    if(desc.collider) (void)desc.collider->ToWorld(desc.transform,
        desc.meshRenderer&&desc.meshRenderer->mesh?&desc.meshRenderer->mesh->Bounds():nullptr);
    if(nextSpawnOrder_==(std::numeric_limits<std::uint64_t>::max)())throw std::overflow_error("Spawn order exhausted.");
    for(std::size_t i=0;i<slots_.size();++i)
    {
        // 世代が0まで周回した席は再利用しない。古いIDが一致する可能性を残さないため。
        auto& slot=slots_[i];if(slot.object||!slot.generation)continue;
        // 文字列などの構築が例外で失敗しても、Pool側が席を返す。
        auto object=pool_.Rent(desc);
        if(!object)throw std::runtime_error("World pool full or SceneArena closing.");
        InvalidateRenderList();slot.object=std::move(object);slot.pendingDestroy=false;
        slot.spawnOrder=nextSpawnOrder_++;++count_;
        // World番号・席番号・現在の世代をセットで返す。
        return {id_,static_cast<std::uint32_t>(i),slot.generation};
    }
    throw std::runtime_error("World capacity reached.");
}
std::vector<ObjectId> World::SpawnBatch(std::span<const ObjectDesc> descriptions)
{
    if (descriptions.size() > GetCapacity() - count_)
        throw std::runtime_error("World batch exceeds available capacity.");
    std::vector<ObjectId> ids;
    ids.reserve(descriptions.size()); // Spawn後の名簿追加で再確保を起こさない。
    try { for (const auto& desc : descriptions) ids.push_back(Spawn(desc)); }
    catch (...)
    {
        InvalidateRenderList();
        // 全員をCommitChangesすると、呼ぶ前からある削除予約まで確定してしまう。
        // ここではBatchで借りた席だけを戻し、IDの世代も通常どおり進める。
        for (const auto id : ids) Release(slots_[id.index]);
        throw;
    }
    return ids;
}
ObjectId World::GetIdAt(std::size_t index) const noexcept
{
    if(index>=slots_.size()||!slots_[index].object||slots_[index].pendingDestroy)return {};
    return {id_,static_cast<std::uint32_t>(index),slots_[index].generation};
}
// 検索の共通条件。別World・範囲外・世代違い・削除予約済みは全部無効。
const World::Slot* World::Locate(ObjectId id) const noexcept
{
    if(id.world!=id_||id.index>=slots_.size())return nullptr;
    const auto& slot=slots_[id.index];
    return slot.object&&slot.generation==id.generation&&!slot.pendingDestroy?&slot:nullptr;
}
// 所有は移さずポインタを貸す。削除をまたいで保存したいならObjectIdを保存する。
GameObject* World::Find(ObjectId id) noexcept{const auto* s=Locate(id);return s?s->object.Get():nullptr;}
// constのWorldからは読み取り用だけを貸す。
const GameObject* World::Find(ObjectId id) const noexcept{const auto* s=Locate(id);return s?s->object.Get():nullptr;}
// その場でデストラクタは呼ばない。更新中に対象を消して走査を壊さないための予約。
bool World::RequestDestroy(ObjectId id) noexcept
{if(!Locate(id))return false;InvalidateRenderList();slots_[id.index].pendingDestroy=true;return true;}
// 本体をPoolへ返し、世代を進め、個数を減らす。返却された席は次の生成に使える。
void World::Release(Slot& slot) noexcept{slot.object.Reset();slot.pendingDestroy=false;++slot.generation;--count_;}
// 区切りで削除を確定。サンプルではPlayingの固定更新から呼ぶ。
void World::CommitChanges() noexcept
{InvalidateRenderList();for(auto& slot:slots_)if(slot.object&&slot.pendingDestroy)Release(slot);}
// 描画依頼側の共有所有も先に外してから、全オブジェクトを返す。
void World::Clear() noexcept
{InvalidateRenderList();for(auto& slot:slots_)if(slot.object)Release(slot);}
// 毎回Worldの現状から作る。前回の行列をそのまま使い続けるキャッシュではない。
std::span<const RenderItem> World::BuildRenderList() const
{
    InvalidateRenderList();auto* items=static_cast<RenderItem*>(renderMemory_.Data());
    for(const auto& slot:slots_)
    {
        // 実体なし・削除予約・描画情報なしなら省略。距離や画面外によるカリングはまだない。
        if(!slot.object||slot.pendingDestroy||!slot.object->meshRenderer)continue;
        const auto& visual=*slot.object->meshRenderer;
        if(!visual.mesh||!visual.render.visible)continue;
        visual.material.Validate();
        // Transformを行列へ変換し、描画情報と生成順をコピーする。
        items[renderCount_++]={slot.object->transform.MakeWorldMatrix(),visual,slot.spawnOrder};
    }
    // 描画レイヤー→同レイヤー内順→生成順。深度ありでは、最終的な前後関係に深度テストも効く。
    std::sort(items,items+renderCount_,[](const auto& a,const auto& b)
    {
        const auto& x=a.renderer.render;const auto& y=b.renderer.render;
        if(x.layer!=y.layer)return x.layer<y.layer;
        if(x.orderInLayer!=y.orderInLayer)return x.orderInLayer<y.orderInLayer;
        return a.spawnOrder<b.spawnOrder;
    });
    return {items,renderCount_};
}
