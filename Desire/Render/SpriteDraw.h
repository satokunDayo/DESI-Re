#pragma once
// 画面に画像を1枚描いてほしい、という依頼書。UIButtonなどの振る舞いはまだ持たない。
#include "Render/GpuTexture.h"
#include <array>
#include "Render/RenderSettings.h"

// 描画依頼。オブジェクト本体・当たり判定・配置レイヤーは別の責務。
// 座標系はRendererのCanvas設定で決まる。原点は左上、+X右、+Y下。
struct SpriteDraw
{
    // 描く画像を共有所有。nullならSpriteRendererが描画を省く。
    GpuTextureHandle texture;
    // ゲーム用SpriteはCanvas単位（既定1280×720）。overlayは窓の実ピクセル単位。
    float x = 0, y = 0, width = 128, height = 128;
    float rotationDegrees = 0; // 中心を軸に時計回り
    // RGBAを画像の色へ掛ける。白なら元の色、alphaを下げれば半透明になる。
    std::array<float, 4> tint{ 1, 1, 1, 1 };
    // 画像全体を0〜1とした切り出し範囲。配列は左・上・幅・高さで、右・下ではない。
    std::array<float, 4> uv{ 0, 0, 1, 1 }; // 左上UV＋幅・高さ
    // 描画レイヤーと順番と可視性。既定ではUI層。配置や衝突のレイヤーとは別の話。
    RenderSettings render{ RenderLayers::UI, 0, true };
};
