#pragma once
// SpriteDrawをDX12の命令へ変換する係。画像ロードや入力判定は担当しない。
#include "SpriteDraw.h"
#include <span>

class SpriteRenderer final
{
public:
    // Root SignatureとPSOを1回作る。以後は画像や位置の定数を差し替えて使い回す。
    void Initialize(ID3D12Device* device);
    // 受け取った依頼を並べ替えて記録する。Queueへの送信とGPU待ちはD3D12Renderer側。
    void Draw(ID3D12GraphicsCommandList* commands, std::span<const SpriteDraw> sprites,
        UINT targetWidth, UINT targetHeight, float canvasWidth = 1280, float canvasHeight = 720) const;
private:
    // root_はシェーダーへ渡す情報の約束、pipeline_はブレンド等を含む描画設定一式。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
};
