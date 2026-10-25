#pragma once
// 画面全体の設定。各オブジェクトの描画順を持つRenderSettingsとは別の係。
#include <cstdint>

// Fullは将来用の名前。今回の実装はNone/Fastで、Fullの指定は例外で知らせる。
enum class CrtQuality { None, Fast, Full };
enum class ResolutionMode { Fixed, Native };
enum class UpscaleMode { IntegerFit, AspectFit };

struct ResolutionSettings
{
    ResolutionMode mode = ResolutionMode::Fixed;
    std::uint32_t internalWidth = 320;
    std::uint32_t internalHeight = 180;
    UpscaleMode upscale = UpscaleMode::AspectFit;
};

struct CrtSettings
{
    CrtQuality quality = CrtQuality::None;
    float scanlineAlpha = 0.4f;     // 0～1。0なら走査線による減光なし。
    float vignetteStrength = 0.4f; // 0～1。画面の隅を少し暗くする。
};

struct RendererSettings
{
    ResolutionSettings resolution;
    CrtSettings crt;
    // 既存サンプルは1280×720で配置済みなので、その座標を維持する。
    // 両方0なら内部解像度をそのままSpriteの座標系に使う。
    std::uint32_t spriteCanvasWidth = 1280;
    std::uint32_t spriteCanvasHeight = 720;
};
