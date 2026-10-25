#pragma once
#include <tuple>
// FBXを読む→三角形へ整理→GPUへ転送→アニメをベイクする、入場時の仕事。
#include "Model/ModelAsset.h"
#include "Resource/ResourceManager.h"
#include <map>
class D3D12Renderer;
class TextureLoader;
class ModelLoader final
{
public:
    ModelLoader(MemoryManager& memory, ResourceManager& resources,
        D3D12Renderer& renderer, TextureLoader& textures)
        : memory_(memory),resources_(resources),renderer_(renderer),textures_(textures) {}
    // Assets相対パス。初版はScene用・同期・メインスレッド。テクスチャは外部画像を読む。
    ModelHandle Load(const std::filesystem::path& path, bool loadTextures = true);
    void PruneExpired();
private:
    MemoryManager& memory_;
    ResourceManager& resources_;
    D3D12Renderer& renderer_;
    TextureLoader& textures_;
    using Key = std::tuple<std::uint64_t,std::filesystem::path,bool>;
    std::map<Key,std::weak_ptr<const ModelAsset>> cache_;
};
