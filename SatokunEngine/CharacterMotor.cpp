#include "CharacterMotor.h"
#include <cmath>
#include <limits>
#include <stdexcept>
CharacterMotor::CharacterMotor(float speed,MovementPlane plane):speed_(speed),plane_(plane)
{
    if(!std::isfinite(speed)||speed<0||(plane!=MovementPlane::XZ&&plane!=MovementPlane::XY&&plane!=MovementPlane::Free3D))
        throw std::invalid_argument("Invalid character movement settings.");
}
MoveResult CharacterMotor::Move(CollisionWorld& collision,ObjectId object,const DirectX::XMFLOAT3& direction,double seconds) const
{
    if(!std::isfinite(seconds)||seconds<0||!std::isfinite(direction.x)||!std::isfinite(direction.y)||!std::isfinite(direction.z))
        throw std::invalid_argument("Character input must be finite; seconds must be nonnegative.");
    double x=direction.x,y=direction.y,z=direction.z;
    if(plane_==MovementPlane::XZ)y=0;
    if(plane_==MovementPlane::XY)z=0;
    const double length=std::sqrt(x*x+y*y+z*z);
    if(length>1){x/=length;y/=length;z/=length;}
    const auto distance=[&](double d)
    {
        const double result=d*speed_*seconds;
        if(!std::isfinite(result)||std::abs(result)>(std::numeric_limits<float>::max)())throw std::overflow_error("Character move overflow.");
        return static_cast<float>(result);
    };
    return collision.MoveAabb(object,{distance(x),distance(y),distance(z)});
}
