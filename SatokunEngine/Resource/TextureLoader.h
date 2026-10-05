#pragma once
// ゲーム側から画像を呼ぶ窓口。ファイル読込・展開・GPU転送の3段階をまとめる。
#include "Render/GpuTexture.h"
#include "Resource/ResourceManager.h"

class D3D12Renderer;
// ファイル→CPU画素→GPU画像をつなぐ。全関数メインスレッド専用。
class TextureLoader final
{
public:
    // 3つのサービスは借りる。Loaderより先に破棄しないようEngineの宣言順で配線している。
    TextureLoader(MemoryManager& memory, ResourceManager& resources, D3D12Renderer& renderer)
        : memory_(memory), resources_(resources), renderer_(renderer) {}
    // 成功したら描画に渡せるGPU画像を返す。同期処理なので大量ロードを毎フレーム呼ぶ用途ではない。
    GpuTextureHandle Load(const std::filesystem::path& relativePath, ArenaType scope = ArenaType::Scene);
    // 実体が消えた目録だけ消す。生存中の画像を回収する関数ではない。
    void PruneExpired();
private:
    // Arena IDとパスで区別。別シーンの同名ファイルを古いArena扱いで使い回さない。
    using Key = std::pair<std::uint64_t, std::filesystem::path>;
    MemoryManager& memory_;
    ResourceManager& resources_;
    D3D12Renderer& renderer_;
    // weakなので、目録だけを理由にGPU画像を常駐させない。持ち主はSceneや描画依頼側。
    std::map<Key, std::weak_ptr<const GpuTexture>> cache_;
};
