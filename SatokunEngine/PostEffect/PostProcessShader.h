#pragma once

//
// このシェーダは「ゲームそのものを描くシェーダ」ではない。
//
// SpriteもTileMapもActorも知らない。
// 既に完成したゲーム画面(RenderTarget)を受け取り、
// 最後にCRT風加工を行ってモニタへ表示するだけである。
//
// CRT Fastの流れ（NoneならPointで読み、そのままBackBufferへ）:
//
// Game Rendering
//      ↓
// sceneImage(RenderTarget)
//      ↓
// Fullscreen Triangle
//      ↓
// Linear Filtering
//      ↓
// Horizontal Blur
//      ↓
// Scanline
//      ↓
// Vignette
//      ↓
// BackBuffer
//      ↓
// Present
//

inline constexpr char PostProcessShader[] = R"(

//
// CPUから描画ごとに渡される4個の定数。
// C++側はSetGraphicsRoot32BitConstantsで渡し、HLSL側はcbufferとして受け取る。
//
// register(b0)
//      ↓
// ルートシグネチャのb0に対応。
// この4値のために別のGPUバッファを確保しているわけではない。
//
cbuffer Post : register(b0)
{
    //
    // x : 予約枠
    //     昔は内部解像度高さを入れていた。
    //     現在は未使用だが互換性維持のため残している。
    //
    // y : Scanline強度
    //
    // z : Vignette強度
    //
    // w : CRT有効フラグ
    //
    float4 parameters;
};

//
// ゲーム描画済み画像。
//
// 例えば
//
// 256x240
// 320x180
// 640x360
//
// などの内部画面が入る。
//
// Spriteはもう全部描画済み。
// TileMapも全部描画済み。
// ここから先は「1枚の画像」として扱う。
//
Texture2D sceneImage : register(t0);

// None用。元の画素をくっきり拡大する係。
SamplerState pointSampler : register(s0);

// Fast用。隣の画素との中間色を作り、CRT風の柔らかい表示にする係。
SamplerState linearSampler : register(s1);

// 名前だけでは補間方法は変わらない。
// PostProcessRenderer.cppで、s0をPoint、s1をLinearとして登録している。

//
// VertexShaderからPixelShaderへ渡す構造体。
//
struct VSOutput
{
    //
    // VSから出す時はクリップ座標。
    // ラスタライズを通ってPSへ届く時は、出力先の画素座標になる。
    //
    float4 position : SV_Position;

    //
    // sceneImageを読むためのUV。
    //
    float2 uv : TEXCOORD0;
};

//
// フルスクリーン三角形生成。
//
// 頂点バッファ不要。
//
// Draw(3,0)すると
//
// id=0
// id=1
// id=2
//
// が自動で渡される。
//
VSOutput VSMain(uint id : SV_VertexID)
{
    //
    // 結果:
    //
    // id=0 -> (0,0)
    // id=1 -> (2,0)
    // id=2 -> (0,2)
    //
    float2 uv = float2(
        (id << 1) & 2,
        id & 2);

    VSOutput o;

    //
    // UVをNDCへ変換。
    //
    // 通常の矩形ではなく
    // "巨大な三角形1枚"
    // を生成している。
    //
    // 生成される頂点:
    //
    // (-1, 1)
    // ( 3, 1)
    // (-1,-3)
    //
    // 画面全体を完全に覆う。
    //
    o.position = float4(
        uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0,
        0.0,
        1.0);

    //
    // UVはそのままPixelShaderへ送る。
    //
    o.uv = uv;

    return o;
}

//
// メインのポストプロセス。
//
float4 PSMain(VSOutput i) : SV_Target
{
    //
    // Scanline強度。
    //
    float scanlineAlpha = parameters.y;

    //
    // Vignette強度。
    //
    float vignetteStrength = parameters.z;

    //
    // CRT有効フラグ。
    //
    float crtEnabled = parameters.w;

    // OFFならここで帰る。ぼかしの計算まで進ませない。
    // Pointで1回読み、画素の色をそのまま表示する。
    if (crtEnabled <= 0.5)
    {
        return float4(sceneImage.SampleLevel(pointSampler, i.uv, 0).rgb, 1.0);
    }

    // ここから先はFast専用。Linearの3回読みで横方向のにじみを作る。

    //
    // 元画像サイズ取得。
    //
    // 例:
    //
    // 256 x 240
    //
    uint width;
    uint height;

    sceneImage.GetDimensions(width, height);

    //
    // UV空間での
    // 「1ピクセル分の移動量」。
    //
    float2 texel =
        1.0 / float2(width, height);

    //
    // ----------------------------------------------------
    // Linear + 横方向Blur
    // ----------------------------------------------------
    //
    // ファミコン実機は
    //
    // PPU
    // ↓
    // コンポジット信号
    // ↓
    // ブラウン管
    //
    // の影響で横方向に若干滲む。
    //
    // それを軽く再現。
    //
    // 3tapフィルタ:
    //
    // 左   15%
    // 中央 70%
    // 右   15%
    //
    // 合計100%
    //

    float3 color =
        sceneImage.SampleLevel(
            linearSampler,
            i.uv,
            0).rgb * 0.70;

    //
    // 右隣。
    //
    color +=
        sceneImage.SampleLevel(
            linearSampler,
            i.uv + float2(texel.x, 0.0),
            0).rgb * 0.15;

    //
    // 左隣。
    //
    color +=
        sceneImage.SampleLevel(
            linearSampler,
            i.uv - float2(texel.x, 0.0),
            0).rgb * 0.15;

    //
    // 走査線と周辺減光を適用。Noneは上でreturn済み。
    //
    if (crtEnabled > 0.5)
    {
        //
        // ------------------------------------------------
        // Scanline
        // ------------------------------------------------
        //
        // 出力画面のY座標を見る。
        //
        // UVではない。
        //
        // これが重要。
        //
        // UVベースだと
        //
        // 200%
        // 300%
        // 125%
        //
        // のような拡大率で縞が崩れる。
        //
        // SV_Positionを使うことで
        //
        // 出力先の物理画素
        //
        // に合わせて縞を作れる。
        //
        // 結果:
        //
        // 行0 → 1
        // 行1 → 0
        // 行2 → 1
        // 行3 → 0
        //
        // 暗
        // 明
        // 暗
        // 明
        //
        float scan =
            1.0 -
            fmod(
                floor(i.position.y),
                2.0);

        //
        // ------------------------------------------------
        // Vignette
        // ------------------------------------------------
        //
        // UV:
        //
        // 0～1
        //
        // を
        //
        // -1～1
        //
        // に変換。
        //
        float2 centered =
            i.uv * 2.0 - 1.0;

        //
        // 中心からの距離計算。
        //
        // dot(v,v)
        //
        // =
        //
        // x²+y²
        //
        // 中央ほど0。
        //
        // 外側ほど1へ近づく。
        //
        float edge =
            saturate(
                dot(centered, centered)
                * 0.5);

        //
        // Scanline適用。
        //
        // scanlineAlpha=0.25なら
        //
        // 暗い行だけ75%の明るさ。
        //
        color *=
            (1.0 -
            scanlineAlpha * scan);

        //
        // 周辺減光。
        //
        // 中央はそのまま。
        // 外周だけ暗くする。
        //
        color *=
            (1.0 -
            vignetteStrength * edge);
    }

    //
    // Alphaは使わない。
    //
    // 完全不透明。
    //
    return float4(
        color,
        1.0);
}

//
// 背景色だけ描画したい時用。
//
// parametersにRGBAを入れておけば
// 画面全体をその色で塗り潰す。
//
float4 BackgroundPS(VSOutput i) : SV_Target
{
    return parameters;
}

)";