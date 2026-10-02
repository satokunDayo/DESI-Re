#include "SceneLayoutLoader.h"
#include "World.h"
#include <cmath>
#include <set>
#include <stdexcept>

ObjectId LayoutInstance::Find(std::size_t layer, std::size_t column, std::size_t row) const noexcept
{
    for (const auto& object : objects_)
        if (object.layerIndex == layer && object.column == column && object.row == row) return object.id;
    return {};
}
void LayoutInstance::RequestDestroy(World& world) const noexcept
{ for (const auto& object : objects_) (void)world.RequestDestroy(object.id); }

LayoutInstance SceneLayoutLoader::Load(World& world, const ObjectFactory& factory, const MapData& map)
{
    if (map.layers.empty()) throw std::invalid_argument("MapData needs at least one layer.");
    std::vector<ObjectDesc> descriptions;
    LayoutInstance result;
    std::set<std::string> names;
    const auto available = world.GetCapacity() - world.GetCount();
    for (std::size_t index = 0; index < map.layers.size(); ++index)
    {
        const auto& layer = map.layers[index];
        if (layer.name.empty() || !names.insert(layer.name).second)
            throw std::invalid_argument("Map layer names must be non-empty and unique.");
        const auto prefix = "Map layer '" + layer.name + "': ";
        if (!layer.grid.Width() || !layer.grid.Height()) throw std::invalid_argument(prefix + "empty grid.");
        if (layer.plane != GridPlane::XY && layer.plane != GridPlane::XZ)
            throw std::invalid_argument(prefix + "invalid grid plane.");
        if (!std::isfinite(layer.cellSize.x) || !std::isfinite(layer.cellSize.y) ||
            layer.cellSize.x <= 0 || layer.cellSize.y <= 0)
            throw std::invalid_argument(prefix + "cell spacing must be finite and positive.");
        // 空欄だけの層でも、非均一スケールや不正な変換を見逃さない。
        try { (void)Transform{}.TransformedBy(layer.transform); }
        catch (const std::exception& error) { throw std::invalid_argument(prefix + error.what()); }
        for (std::size_t row = 0; row < layer.grid.Height(); ++row)
            for (std::size_t column = 0; column < layer.grid.Width(); ++column)
            {
                const char symbol = layer.grid.At(column, row);
                try
                {
                    const auto* definition = layer.symbols.Find(symbol);
                    if (!definition) throw std::invalid_argument("Unregistered symbol '" + std::string(1, symbol) + "'.");
                    if (definition->prefab.empty()) continue;
                    if (descriptions.size() == available) throw std::runtime_error("World capacity is insufficient.");
                    auto desc = factory.MakeDescription(definition->prefab);
                    Transform cell;
                    const auto x = static_cast<float>(double(column) * layer.cellSize.x);
                    const auto next = static_cast<float>(double(row) * layer.cellSize.y);
                    cell.SetPosition(layer.plane == GridPlane::XY ? DirectX::XMFLOAT3{x,-next,0} : DirectX::XMFLOAT3{x,0,next});
                    // 原型内のオフセット→マスの位置→層全体の回転・移動。位置だけでなく向きも合成。
                    desc.transform = desc.transform.TransformedBy(cell).TransformedBy(layer.transform);
                    desc.placementLayer = layer.placementLayer;
                    if (desc.meshRenderer)
                    {
                        auto& render = desc.meshRenderer->render;
                        if (layer.renderLayer) render.layer = *layer.renderLayer;
                        if (layer.orderInLayer) render.orderInLayer = *layer.orderInLayer;
                        if (definition->renderLayer) render.layer = *definition->renderLayer;
                        if (definition->orderInLayer) render.orderInLayer = *definition->orderInLayer;
                        desc.meshRenderer->material.Validate();
                    }
                    if (desc.collider) (void)desc.collider->ToWorld(desc.transform,
                        desc.meshRenderer && desc.meshRenderer->mesh ? &desc.meshRenderer->mesh->Bounds() : nullptr);
                    descriptions.push_back(std::move(desc));
                    result.objects_.push_back({{}, index, column, row, symbol});
                }
                catch (const std::exception& error)
                {
                    throw std::runtime_error(prefix + "row " + std::to_string(row + 1) + ", column " +
                        std::to_string(column + 1) + ": " + error.what());
                }
            }
    }
    // ここまでは注文書だけ。実体の生成中に失敗してもBatchが今回の分だけ戻す。
    const auto ids = world.SpawnBatch(descriptions);
    for (std::size_t i = 0; i < ids.size(); ++i) result.objects_[i].id = ids[i];
    return result;
}
