#pragma once
// 入力の方向と秒数を移動量へ変換する係。キー番号・カメラ・ゲームの勝敗は知らない。
#include "CollisionWorld.h"
enum class MovementPlane { XZ, XY, Free3D };

class CharacterMotor final
{
public:
    explicit CharacterMotor(float speed=3.0f,MovementPlane plane=MovementPlane::XZ);
    // 斜め入力でも速くならない。長さ1未満ならアナログ入力の強さを残す。
    MoveResult Move(CollisionWorld& collision,ObjectId object,const DirectX::XMFLOAT3& direction,double seconds) const;
    // 初版は指定平面内の移動。重力・ジャンプ・段差・剛体の押し合いはまだ持たない。
private:
    float speed_;
    MovementPlane plane_;
};
