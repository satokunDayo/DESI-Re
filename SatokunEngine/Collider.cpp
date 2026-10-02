#include "Collider.h"
#include "CpuMesh.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    float Checked(double value)
    {
        if (!std::isfinite(value) || std::abs(value) > (std::numeric_limits<float>::max)())
            throw std::overflow_error("Collider geometry overflow.");
        return static_cast<float>(value);
    }
    void Finite(const DirectX::XMFLOAT3& v)
    {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            throw std::invalid_argument("Collider requires finite vectors.");
    }
    void CheckWorld(const WorldCollider& c)
    {
        Finite(c.center); Finite(c.halfExtents);
        if ((c.shape != ColliderShape::Aabb && c.shape != ColliderShape::Sphere) ||
            c.halfExtents.x < 0 || c.halfExtents.y < 0 || c.halfExtents.z < 0 ||
            !std::isfinite(c.radius) || c.radius < 0)
            throw std::invalid_argument("Invalid world collider.");
    }
    bool Compare(double a, double b, bool touching) { return touching ? a <= b : a < b; }
    bool Test(const WorldCollider& a, const WorldCollider& b, bool touching)
    {
        CheckWorld(a); CheckWorld(b);
        const double dx = double(a.center.x) - b.center.x;
        const double dy = double(a.center.y) - b.center.y;
        const double dz = double(a.center.z) - b.center.z;
        if (a.shape == ColliderShape::Aabb && b.shape == ColliderShape::Aabb)
            return Compare(std::abs(dx), double(a.halfExtents.x) + b.halfExtents.x, touching) &&
                Compare(std::abs(dy), double(a.halfExtents.y) + b.halfExtents.y, touching) &&
                Compare(std::abs(dz), double(a.halfExtents.z) + b.halfExtents.z, touching);
        if (a.shape == ColliderShape::Sphere && b.shape == ColliderShape::Sphere)
        {
            const double r = double(a.radius) + b.radius;
            return Compare(dx*dx + dy*dy + dz*dz, r*r, touching);
        }
        const auto& box = a.shape == ColliderShape::Aabb ? a : b;
        const auto& sphere = a.shape == ColliderShape::Sphere ? a : b;
        // 球の中心から箱の一番近い点までの距離。箱の中に中心があれば距離0。
        const double x = (std::max)(std::abs(dx) - box.halfExtents.x, 0.0);
        const double y = (std::max)(std::abs(dy) - box.halfExtents.y, 0.0);
        const double z = (std::max)(std::abs(dz) - box.halfExtents.z, 0.0);
        return Compare(x*x + y*y + z*z, double(sphere.radius)*sphere.radius, touching);
    }
}

Collider Collider::Box(DirectX::XMFLOAT3 half, DirectX::XMFLOAT3 origin, ColliderMode kind)
{ Collider c; c.halfExtents=half; c.center=origin; c.mode=kind; c.Validate(); return c; }
Collider Collider::Sphere(float r, DirectX::XMFLOAT3 origin, ColliderMode kind)
{ Collider c; c.shape=ColliderShape::Sphere; c.radius=r; c.center=origin; c.mode=kind; c.Validate(); return c; }
Collider Collider::AutoFit(ColliderShape type, ColliderMode kind)
{ Collider c; c.autoFit=true; c.shape=type; c.mode=kind; c.Validate(); return c; }
void Collider::Validate() const
{
    Finite(center); Finite(halfExtents);
    if ((shape!=ColliderShape::Aabb && shape!=ColliderShape::Sphere) ||
        (mode!=ColliderMode::Solid && mode!=ColliderMode::Trigger) ||
        halfExtents.x<0 || halfExtents.y<0 || halfExtents.z<0 || !std::isfinite(radius) || radius<=0)
        throw std::invalid_argument("Invalid collider description.");
}
WorldCollider Collider::ToWorld(const Transform& transform, const MeshBounds* bounds) const
{
    Validate();
    auto localCenter=center, half=halfExtents;
    float localRadius=radius;
    if (autoFit)
    {
        if (!bounds) throw std::invalid_argument("AutoFit requires a mesh with bounds.");
        Finite(bounds->min); Finite(bounds->max);
        if (bounds->min.x>bounds->max.x || bounds->min.y>bounds->max.y || bounds->min.z>bounds->max.z)
            throw std::invalid_argument("Invalid mesh bounds for AutoFit.");
        localCenter={Checked((double(bounds->min.x)+bounds->max.x)*.5),
            Checked((double(bounds->min.y)+bounds->max.y)*.5),Checked((double(bounds->min.z)+bounds->max.z)*.5)};
        half={Checked((double(bounds->max.x)-bounds->min.x)*.5),
            Checked((double(bounds->max.y)-bounds->min.y)*.5),Checked((double(bounds->max.z)-bounds->min.z)*.5)};
        if (shape==ColliderShape::Sphere)
            localRadius=Checked(std::sqrt(double(half.x)*half.x+double(half.y)*half.y+double(half.z)*half.z));
    }
    const auto m=transform.MakeWorldMatrix();
    WorldCollider result; result.shape=shape;
    result.center={Checked(double(localCenter.x)*m._11+double(localCenter.y)*m._21+double(localCenter.z)*m._31+m._41),
        Checked(double(localCenter.x)*m._12+double(localCenter.y)*m._22+double(localCenter.z)*m._32+m._42),
        Checked(double(localCenter.x)*m._13+double(localCenter.y)*m._23+double(localCenter.z)*m._33+m._43)};
    // 回した箱の8隅を囲むAABBと同じ結果。斜めの壁をOBBで精密判定する処理ではない。
    result.halfExtents={Checked(double(half.x)*std::abs(m._11)+double(half.y)*std::abs(m._21)+double(half.z)*std::abs(m._31)),
        Checked(double(half.x)*std::abs(m._12)+double(half.y)*std::abs(m._22)+double(half.z)*std::abs(m._32)),
        Checked(double(half.x)*std::abs(m._13)+double(half.y)*std::abs(m._23)+double(half.z)*std::abs(m._33))};
    const auto scale=transform.GetScale();
    if (shape==ColliderShape::Sphere) result.radius=Checked(double(localRadius)*(std::max)({scale.x,scale.y,scale.z}));
    CheckWorld(result); return result;
}
bool Collision::Overlaps(const WorldCollider& a,const WorldCollider& b){return Test(a,b,true);}
bool Collision::Penetrates(const WorldCollider& a,const WorldCollider& b){return Test(a,b,false);}
