#include "Game/AI/MazeNavigation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    bool Finite(const DirectX::XMFLOAT3& p)
    { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
}

MazeNavigation::MazeNavigation(const MapGrid& map, float cellSize)
    : map_(map), width_(static_cast<int>(map.Width())),
      height_(static_cast<int>(map.Height())), cellSize_(cellSize)
{
    if (width_ <= 0 || height_ <= 0 || !std::isfinite(cellSize_) || cellSize_ <= 0)
        throw std::invalid_argument("MazeNavigation needs a map and positive cell size.");
}

bool MazeNavigation::Walkable(MazeCell c) const
{
    return c.x >= 0 && c.z >= 0 && c.x < width_ && c.z < height_ &&
        map_.At(c.x, c.z) != '#';
}

MazeCell MazeNavigation::ToCell(const DirectX::XMFLOAT3& p) const
{
    if (!Finite(p)) throw std::invalid_argument("Maze position must be finite.");
    // タイル中心は0,2,4...。境界はその中間なので、+0.5して切り下げる。
    const double x = std::floor(double(p.x) / cellSize_ + 0.5);
    const double z = std::floor(double(p.z) / cellSize_ + 0.5);
    if (x < -1 || z < -1 || x > width_ || z > height_) return {-1, -1};
    return {static_cast<int>(x), static_cast<int>(z)};
}

DirectX::XMFLOAT3 MazeNavigation::Position(MazeCell c) const
{ return {c.x * cellSize_, 0, c.z * cellSize_}; }
int MazeNavigation::Index(MazeCell c) const { return c.z * width_ + c.x; }
MazeCell MazeNavigation::Cell(int index) const { return {index % width_, index / width_}; }

MazeNavigation::Search MazeNavigation::Explore(MazeCell start) const
{
    if (!Walkable(start)) throw std::invalid_argument("Path must start in a walkable cell.");
    Search search;
    search.parent.assign(std::size_t(width_) * height_, -1);
    search.distance.assign(search.parent.size(), -1);
    search.visited.reserve(search.parent.size());
    const int first = Index(start);
    search.parent[first] = first;
    search.distance[first] = 0;
    search.visited.push_back(first);
    constexpr std::array<MazeCell, 4> directions{{{0,-1},{1,0},{0,1},{-1,0}}};
    for (std::size_t head = 0; head < search.visited.size(); ++head)
    {
        const int current = search.visited[head];
        const auto c = Cell(current);
        for (auto d : directions)
        {
            const MazeCell next{c.x + d.x, c.z + d.z};
            if (!Walkable(next)) continue;
            const int index = Index(next);
            if (search.distance[index] >= 0) continue;
            search.parent[index] = current;
            search.distance[index] = search.distance[current] + 1;
            search.visited.push_back(index);
        }
    }
    return search;
}

std::vector<MazeCell> MazeNavigation::RouteToEstimate(MazeCell start,
    const DirectX::XMFLOAT3& estimate) const
{
    if (!Finite(estimate)) throw std::invalid_argument("Target estimate must be finite.");
    const auto search = Explore(start);
    int best = Index(start);
    double bestError = (std::numeric_limits<double>::infinity)();
    for (int index : search.visited)
    {
        const auto p = Position(Cell(index));
        const double dx = double(p.x) - estimate.x, dz = double(p.z) - estimate.z;
        const double error = dx*dx + dz*dz;
        if (error < bestError || (error == bestError && search.distance[index] < search.distance[best]))
        { best = index; bestError = error; }
    }
    std::vector<MazeCell> route;
    for (int index = best; index != Index(start); index = search.parent[index])
        route.push_back(Cell(index));
    std::reverse(route.begin(), route.end());
    return route;
}

MazeCell MazeNavigation::RandomSpawn(MazeCell player, int minimumSteps, std::mt19937& random) const
{
    if (minimumSteps < 1) throw std::invalid_argument("Spawn distance must be positive.");
    const auto search = Explore(player);
    std::vector<int> candidates, farthest;
    int farthestDistance = -1;
    for (int index : search.visited)
    {
        const auto cell = Cell(index);
        // プレイヤーとゴールのマスは使わない。現在の迷路の通常通路は'.'。
        if (cell == player || map_.At(cell.x, cell.z) != '.') continue;
        const int distance = search.distance[index];
        if (distance >= minimumSteps) candidates.push_back(index);
        if (distance > farthestDistance) { farthest.clear(); farthestDistance = distance; }
        if (distance == farthestDistance) farthest.push_back(index);
    }
    // 小さな地図でも無限に引き直さない。距離条件を満たせなければ最遠の候補を使う。
    if (candidates.empty()) candidates = std::move(farthest);
    if (candidates.empty()) throw std::runtime_error("No reachable '.' tile for enemy spawn.");
    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
    return Cell(candidates[pick(random)]);
}

bool MazeNavigation::HasLineOfSight(const DirectX::XMFLOAT3& from,
    const DirectX::XMFLOAT3& to) const
{
    auto cell = ToCell(from);
    const auto end = ToCell(to);
    if (!Walkable(cell) || !Walkable(end)) return false;
    const double x = double(from.x) / cellSize_ + 0.5;
    const double z = double(from.z) / cellSize_ + 0.5;
    const double dx = (double(to.x) - from.x) / cellSize_;
    const double dz = (double(to.z) - from.z) / cellSize_;
    const int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0;
    const int sz = dz > 0 ? 1 : dz < 0 ? -1 : 0;
    const double infinity = (std::numeric_limits<double>::infinity)();
    const double deltaX = sx ? 1 / std::abs(dx) : infinity;
    const double deltaZ = sz ? 1 / std::abs(dz) : infinity;
    double nextX = sx ? ((sx > 0 ? cell.x + 1.0 : cell.x) - x) / dx : infinity;
    double nextZ = sz ? ((sz > 0 ? cell.z + 1.0 : cell.z) - z) / dz : infinity;
    while (!(cell == end))
    {
        if (std::abs(nextX - nextZ) < 1e-10)
        {
            // 角をまたぐときは、両隣の壁も確認する。
            if (!Walkable({cell.x + sx, cell.z}) || !Walkable({cell.x, cell.z + sz})) return false;
            cell.x += sx; cell.z += sz; nextX += deltaX; nextZ += deltaZ;
        }
        else if (nextX < nextZ) { cell.x += sx; nextX += deltaX; }
        else { cell.z += sz; nextZ += deltaZ; }
        if (!Walkable(cell)) return false;
    }
    return true;
}
