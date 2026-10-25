#include "Render/D3D12Renderer.h"
#include <cmath>
#include <stdexcept>

// 入力の検査はその場で。GPU画像の交換だけをフレーム先頭まで待たせる。
void D3D12Renderer::SetRendererSettings(const RendererSettings& settings)
{
    const auto& r = settings.resolution;
    if ((r.mode != ResolutionMode::Fixed && r.mode != ResolutionMode::Native) ||
        (r.upscale != UpscaleMode::IntegerFit && r.upscale != UpscaleMode::AspectFit) ||
        !r.internalWidth || !r.internalHeight || r.internalWidth > 16384 || r.internalHeight > 16384)
        throw std::invalid_argument("Invalid internal resolution settings.");
    const auto& c = settings.crt;
    if (c.quality != CrtQuality::None && c.quality != CrtQuality::Fast)
        throw std::invalid_argument("CRT Full is not implemented yet; use None or Fast.");
    if (!std::isfinite(c.scanlineAlpha) || c.scanlineAlpha < 0 || c.scanlineAlpha > 1 ||
        !std::isfinite(c.vignetteStrength) || c.vignetteStrength < 0 || c.vignetteStrength > 1)
        throw std::invalid_argument("CRT strengths must be finite values from zero to one.");
    if ((settings.spriteCanvasWidth == 0) != (settings.spriteCanvasHeight == 0) ||
        settings.spriteCanvasWidth > 16384 || settings.spriteCanvasHeight > 16384)
        throw std::invalid_argument("Use positive canvas dimensions, or zero for both dimensions.");
    requestedSettings_ = settings;
}

void D3D12Renderer::SetInternalResolution(UINT width, UINT height)
{
    auto settings = requestedSettings_;
    settings.resolution.mode = ResolutionMode::Fixed;
    settings.resolution.internalWidth = width; settings.resolution.internalHeight = height;
    SetRendererSettings(settings);
}
void D3D12Renderer::SetNativeResolution()
{
    auto settings = requestedSettings_;
    settings.resolution.mode = ResolutionMode::Native;
    SetRendererSettings(settings);
}
void D3D12Renderer::SetCrtQuality(CrtQuality quality)
{
    auto settings = requestedSettings_; settings.crt.quality = quality;
    SetRendererSettings(settings);
}
void D3D12Renderer::SetSpriteCanvasSize(UINT width, UINT height)
{
    auto settings = requestedSettings_;
    settings.spriteCanvasWidth = width; settings.spriteCanvasHeight = height;
    SetRendererSettings(settings);
}

// 呼ぶ前にFlushする。新しい一式が完成するまでは古い一式を手放さない。
void D3D12Renderer::ApplyRendererSettings()
{
    const auto& r = requestedSettings_.resolution;
    const UINT width = r.mode == ResolutionMode::Native ? width_ : r.internalWidth;
    const UINT height = r.mode == ResolutionMode::Native ? height_ : r.internalHeight;
    if (!internalTarget_ || internalTarget_->Width() != width || internalTarget_->Height() != height)
    {
        try
        {
            auto next = std::make_unique<InternalRenderTarget>(device_.Get(), gpuMemory_, width, height);
            internalTarget_ = std::move(next);
        }
        catch (...)
        {
            // メモリ不足などで交換に失敗。既存の画像と有効な設定は残す。
            requestedSettings_ = activeSettings_;
            throw;
        }
    }
    activeSettings_ = requestedSettings_;
}
