#pragma once
// 地図をWorldへ配置する係。地図から生まれた後も、物は普通のGameObjectとして扱える。
#include "World/MapData.h"
#include "World/ObjectFactory.h"
class World;

struct LayoutObject
{
    ObjectId id;
    std::size_t layerIndex = 0, column = 0, row = 0; // 0始まりの元マス。移動後の位置ではない。
    char symbol = ' ';
};
class LayoutInstance final
{
public:
    std::span<const LayoutObject> Objects() const noexcept { return objects_; }
    ObjectId Find(std::size_t layerIndex, std::size_t column, std::size_t row) const noexcept;
    // この配置で作った物だけ削除予約。World.CommitChangesは呼び出し側の区切りで行う。
    // 名簿は残す。別Worldや、既に消えた世代のIDを渡しても別の物を消さない。
    void RequestDestroy(World& world) const noexcept;
private:
    friend class SceneLayoutLoader;
    // これはIDの名簿で、GameObjectの所有者ではない。破棄してもWorldの物は残る。
    std::vector<LayoutObject> objects_;
};
class SceneLayoutLoader final
{
public:
    // メインスレッド専用。全レイヤーを検査してからまとめて生成。失敗時は元のWorldを保つ。
    static LayoutInstance Load(World& world, const ObjectFactory& factory, const MapData& map);
};
