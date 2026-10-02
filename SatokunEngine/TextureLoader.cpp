// ファイル→CPU画素→UPLOAD→GPU画像。Loadが戻る時点では転送完了を待ち終えている。
#include "TextureLoader.h"
#include "CpuImage.h"
#include "D3D12Renderer.h"

// GPU側の管理はメインスレッド専用。この関数を複数ワーカーから同時に呼ばない。
GpuTextureHandle TextureLoader::Load(const std::filesystem::path& relativePath, ArenaType scope)
{
    PruneExpired();
    const auto arena = memory_.GetArena(scope);
    // 閉じる途中のSceneへの新しいロードを拒否。GPUキャッシュ命中時も同じ規則。
    // キャッシュに当たる場合も閉店中のSceneには渡さない。作業札で閉店との順番をそろえる。
    auto operation = arena->BeginOperation();
    if (!operation) throw std::runtime_error("Cannot load a texture into a closing arena.");
    const Key key{ arena->GetSnapshot().id, relativePath.lexically_normal() };
    // weakを一時的に強い所有へ変換。まだ誰かが使っていれば、同じGPU画像を返す。
    if (const auto found = cache_.find(key); found != cache_.end())
        if (auto texture = found->second.lock()) return texture;
    // まず元ファイルを共有して読む。これはGPU画像とは別のCPUキャッシュ。
    const auto binary = resources_.AcquireBinary(relativePath, scope);
    // RGBA8へ展開。CPUの元ファイルと展開画素を同時に持つ時間がある。
    auto image = CpuImage::Decode(*binary, memory_, scope);
    // DX12側へコピーする。この後はCPU画素を返してもGPU画像は残る。
    auto texture = renderer_.UploadImage(image);
    cache_[key] = texture;
    // image.pixelsはArenaへ戻る。元ファイルはResourceManagerの回収規則に従う。
    return texture;
}

// 有効期限切れというより、所有者ゼロになったweak目録の掃除。時間判定はしない。
void TextureLoader::PruneExpired()
{
    std::erase_if(cache_, [](const auto& entry) { return entry.second.expired(); });
}
