#include "Physics/CollisionWorld.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace
{
    template<class T> MemoryBlock Buffer(MemoryManager& memory,std::size_t count)
    {
        if (!count || count>BlockAllocator::MaxAllocationSize/sizeof(T))
            throw std::invalid_argument("Collision buffer capacity out of range.");
        auto block=memory.Allocate(ArenaType::Scene,count*sizeof(T),alignof(T));
        if (!block) throw std::bad_alloc();
        std::uninitialized_value_construct_n(static_cast<T*>(block.Data()),count);
        return block;
    }
    std::size_t PairCapacity(std::size_t count)
    {
        if (!count || count>BlockAllocator::MaxAllocationSize/(2*sizeof(TriggerEvent)))
            throw std::invalid_argument("Trigger pair capacity out of range.");
        return count;
    }
    double Axis(const DirectX::XMFLOAT3& v,int axis){return axis==0?v.x:axis==1?v.y:v.z;}
    void SetAxis(DirectX::XMFLOAT3& v,int axis,float value)
    { if(axis==0)v.x=value;else if(axis==1)v.y=value;else v.z=value; }
    float Checked(double value)
    {
        if(!std::isfinite(value)||std::abs(value)>(std::numeric_limits<float>::max)())
            throw std::overflow_error("Movement overflow.");
        return static_cast<float>(value);
    }
}
CollisionWorld::CollisionWorld(MemoryManager& memory,World& world,std::size_t pairs)
    :world_(world),capacity_(world.GetCapacity()),pairCapacity_(PairCapacity(pairs)),
    bodies_(Buffer<Body>(memory,capacity_)),previous_(Buffer<Pair>(memory,pairCapacity_)),
    next_(Buffer<Pair>(memory,pairCapacity_)),events_(Buffer<TriggerEvent>(memory,2*pairCapacity_)){}
CollisionWorld::~CollisionWorld()
{
    std::destroy_n(static_cast<Body*>(bodies_.Data()),capacity_);
    std::destroy_n(static_cast<Pair*>(previous_.Data()),pairCapacity_);
    std::destroy_n(static_cast<Pair*>(next_.Data()),pairCapacity_);
    std::destroy_n(static_cast<TriggerEvent*>(events_.Data()),2*pairCapacity_);
}
bool CollisionWorld::Less(const Pair& x,const Pair& y) noexcept
{
    return std::tie(x.a.world,x.a.index,x.a.generation,x.b.world,x.b.index,x.b.generation)<
        std::tie(y.a.world,y.a.index,y.a.generation,y.b.world,y.b.index,y.b.generation);
}
bool CollisionWorld::Allows(const Body& a,const Body& b) noexcept
{return (a.category&b.mask)!=0 && (b.category&a.mask)!=0;}
void CollisionWorld::Collect()
{
    bodyCount_=0;auto* bodies=static_cast<Body*>(bodies_.Data());
    for(std::size_t i=0;i<capacity_;++i)
    {
        const auto id=world_.GetIdAt(i);const auto* object=world_.Find(id);
        if(!object||!object->collider||!object->collider->enabled)continue;
        const auto& c=*object->collider;
        const auto* bounds=object->meshRenderer&&object->meshRenderer->mesh?&object->meshRenderer->mesh->Bounds():nullptr;
        bodies[bodyCount_++]={id,c.ToWorld(object->transform,bounds),c.mode,c.category,c.mask};
    }
}
void CollisionWorld::Reset() noexcept{bodyCount_=pairCount_=eventCount_=0;}
std::span<const TriggerEvent> CollisionWorld::Step()
{
    eventCount_=0;Collect();
    const auto* bodies=static_cast<const Body*>(bodies_.Data());
    auto* next=static_cast<Pair*>(next_.Data());std::size_t nextCount=0;
    for(std::size_t i=0;i<bodyCount_;++i)for(std::size_t j=i+1;j<bodyCount_;++j)
    {
        const auto& a=bodies[i];const auto& b=bodies[j];
        if((a.mode!=ColliderMode::Trigger&&b.mode!=ColliderMode::Trigger)||!Allows(a,b)||!Collision::Overlaps(a.shape,b.shape))continue;
        if(nextCount==pairCapacity_)throw std::runtime_error("Trigger pair capacity exceeded; increase maxTriggerPairs.");
        next[nextCount++]={a.id,b.id};
    }
    std::sort(next,next+nextCount,Less);
    const auto* previous=static_cast<const Pair*>(previous_.Data());
    auto* events=static_cast<TriggerEvent*>(events_.Data());
    std::size_t i=0,j=0;
    // 前回だけ=Exit、今回だけ=Enter、両方=Stay。初回EnterにStayを重ねて送らない。
    while(i<pairCount_||j<nextCount)
    {
        if(j==nextCount||(i<pairCount_&&Less(previous[i],next[j])))
        {events[eventCount_++]={TriggerPhase::Exit,previous[i].a,previous[i].b};++i;}
        else if(i==pairCount_||Less(next[j],previous[i]))
        {events[eventCount_++]={TriggerPhase::Enter,next[j].a,next[j].b};++j;}
        else {events[eventCount_++]={TriggerPhase::Stay,next[j].a,next[j].b};++i;++j;}
    }
    std::swap(previous_,next_);pairCount_=nextCount;
    return {events,eventCount_};
}
MoveResult CollisionWorld::MoveAabb(ObjectId id,const DirectX::XMFLOAT3& delta,float skin)
{
    for(float v:{delta.x,delta.y,delta.z,skin})if(!std::isfinite(v))throw std::invalid_argument("Movement must be finite.");
    if(skin<0)throw std::invalid_argument("Skin must be nonnegative.");
    Collect();auto* bodies=static_cast<Body*>(bodies_.Data());
    const auto end=bodies+bodyCount_;
    const auto found=std::find_if(bodies,end,[id](const Body& b){return b.id==id;});
    if(found==end||found->mode!=ColliderMode::Solid||found->shape.shape!=ColliderShape::Aabb)
        throw std::invalid_argument("MoveAabb needs a live enabled AABB Solid.");
    MoveResult result;result.requested=delta;auto mover=*found;
    const auto origin=world_.Find(id)->transform.GetPosition();
    for(const auto* other=bodies;other!=end;++other)
        if(other->id!=id&&other->mode==ColliderMode::Solid&&Allows(mover,*other)&&Collision::Penetrates(mover.shape,other->shape))
        {result.startedOverlapping=true;return result;}
    // 最初から壁の中なら動かさず知らせる。勝手な押し出しで隣の壁へ埋めない。
    for(int axis=0;axis<3;++axis)
    {
        const double requested=Axis(delta,axis);double allowed=requested;
        if(requested==0)continue;
        const double center=Axis(mover.shape.center,axis),half=Axis(mover.shape.halfExtents,axis);
        for(const auto* other=bodies;other!=end;++other)
        {
            if(other->id==id||other->mode!=ColliderMode::Solid||!Allows(mover,*other))continue;
            const auto& shape=other->shape;double extent=0;
            if(shape.shape==ColliderShape::Aabb)
            {
                bool across=true;
                for(int k=0;k<3;++k)if(k!=axis)
                    across=across&&std::abs(Axis(mover.shape.center,k)-Axis(shape.center,k))<
                        Axis(mover.shape.halfExtents,k)+Axis(shape.halfExtents,k);
                if(!across)continue;
                extent=Axis(shape.halfExtents,axis);
            }
            else
            {
                // 他の2軸から球までの距離を固定し、この軸上で球に当たる区間を求める。
                double distanceSquared=0;
                for(int k=0;k<3;++k)if(k!=axis)
                {
                    const double d=(std::max)(std::abs(Axis(mover.shape.center,k)-Axis(shape.center,k))-Axis(mover.shape.halfExtents,k),0.0);
                    distanceSquared+=d*d;
                }
                const double radiusSquared=double(shape.radius)*shape.radius;
                if(distanceSquared>=radiusSquared)continue;
                extent=std::sqrt(radiusSquared-distanceSquared);
            }
            const double entry=Axis(shape.center,axis)-extent-half,exit=Axis(shape.center,axis)+extent+half;
            if(requested>0&&center<=entry&&center+allowed>=entry)
                allowed=(std::min)(allowed,(std::max)(0.0,entry-center-skin));
            if(requested<0&&center>=exit&&center+allowed<=exit)
                allowed=(std::max)(allowed,(std::min)(0.0,exit-center+skin));
        }
        const float moved=Checked(allowed);
        SetAxis(result.actual,axis,moved);
        SetAxis(mover.shape.center,axis,Checked(center+moved));
        const bool blocked=std::abs(allowed-requested)>1e-6;
        if(axis==0)result.blockedX=blocked;else if(axis==1)result.blockedY=blocked;else result.blockedZ=blocked;
    }
    // 全軸の計算が成功してから実体へ反映。途中の検査失敗で半分だけ動かさない。
    world_.Find(id)->transform.SetPosition({Checked(double(origin.x)+result.actual.x),
        Checked(double(origin.y)+result.actual.y),Checked(double(origin.z)+result.actual.z)});
    return result;
}
