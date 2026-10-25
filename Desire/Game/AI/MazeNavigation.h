#pragma once
#include "World/MapData.h"
#include <random>
#include <vector>

struct MazeCell
{
    int x = 0, z = 0;
    bool operator==(const MazeCell&) const = default;
};

// 文字マップを使う、平らなXZ迷路専用の道案内。
// World、モデル、描画、AIの状態は知らない。
class MazeNavigation final
{
public:
    // mapは借り物。利用中は内容を変えず、先に破棄しない。
    MazeNavigation(const MapGrid& map, float cellSize);
    bool Walkable(MazeCell cell) const;
    MazeCell ToCell(const DirectX::XMFLOAT3& position) const;
    DirectX::XMFLOAT3 Position(MazeCell cell) const;

    // 誤差付き座標に最も近い「到達可能な通路」を目的地にする。
    // 4方向・同じ移動コストなのでBFSで最短経路になる。開始マスは戻り値に含めない。
    std::vector<MazeCell> RouteToEstimate(MazeCell start,
        const DirectX::XMFLOAT3& estimate) const;
    MazeCell RandomSpawn(MazeCell player, int minimumSteps, std::mt19937& random) const;

    // 線分が通るマスを調べる。角で接する壁の隙間も見通さない。
    // 壁は視点より高い前提。多階層、窓、可動扉の判定は対象外。
    bool HasLineOfSight(const DirectX::XMFLOAT3& from,
        const DirectX::XMFLOAT3& to) const;

private:
    struct Search
    {
        std::vector<int> parent;
        std::vector<int> distance;
        std::vector<int> visited;
    };
    int Index(MazeCell cell) const;
    MazeCell Cell(int index) const;
    Search Explore(MazeCell start) const;
    const MapGrid& map_;
    int width_, height_;
    float cellSize_;
};
