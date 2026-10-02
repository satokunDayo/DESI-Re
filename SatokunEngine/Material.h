#pragma once
// 形状に与える見た目。位置はTransform、形はGpuMesh、色や画像はこちらで決める。
#include "GpuTexture.h"
#include <DirectXMath.h>

// Opaqueは不透明、AlphaClipは透明部分を穴抜き。中間alphaで透ける3D合成はまだない。
enum class SurfaceMode { Opaque, AlphaClip };
struct Material
{
    // 画像がなくても単色で描ける。複数Materialで同じ画像を共有してよい。
    GpuTextureHandle texture; // 空なら単色
    // RGBを画像の色へ掛ける。白ならそのまま。3D材質の色にはalpha成分を持たせていない。
    DirectX::XMFLOAT3 color{ 1,1,1 };
    // 簡単な方向光を使うか。falseなら明るさを一定にし、画像の色を照明で暗くしない。
    bool lit = true;
    // UVの倍率とずらし量。Worldのサンプラは繰り返しなので床へ同じ柄を並べられる。
    DirectX::XMFLOAT2 uvScale{ 1,1 };
    DirectX::XMFLOAT2 uvOffset{ 0,0 };
    SurfaceMode surface = SurfaceMode::Opaque;
    // AlphaClip時に、画像alphaがこの値より小さいピクセルを捨てる。
    float alphaCutoff = 0.5f; // AlphaClipで透明部分を抜くしきい値
    // 値の検査。現在の実装場所はWorld.cppなので、Material.cppを探しても見つからない。
    void Validate() const;
};
