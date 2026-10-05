#pragma once
#include "PostEffect/InternalRenderTarget.h"
#include "Render/RendererSettings.h"

// 内部画像を窓へ拡大する係。RTの寿命やPresentはD3D12Rendererが管理する。
class PostProcessRenderer final
{
public:
    void Initialize(ID3D12Device* device);
    // Sceneごとに変わる背景色を描く。色の変更だけでRTを再確保しないための単色パス。
    void FillBackground(ID3D12GraphicsCommandList* commands, UINT width, UINT height,
        const std::array<float, 4>& color) const;
    void Draw(ID3D12GraphicsCommandList* commands, const InternalRenderTarget& source,
        UINT windowWidth, UINT windowHeight, const RendererSettings& settings) const;
    // IntegerFitでは整数位置・整数寸法。入らないときは縦横比を保って縮小する。
    static D3D12_VIEWPORT MakeViewport(UINT sourceWidth, UINT sourceHeight,
        UINT windowWidth, UINT windowHeight, UpscaleMode mode);
private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> backgroundPipeline_;
};
