// 画面座標の画像描画。3Dの床や壁に貼る画像はWorld側のMaterialとして扱う。
#include "SpriteRenderer.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")
namespace
{
    // 頂点バッファ不要の共通Quad。画像ごとの大きさ・角度は定数で渡す。
    // この文字列の中身はC++ではなくHLSL。実行時に下のD3DCompileで変換する。
    // VSMainは頂点番号0〜5から四角形の三角形2枚を作り、中心回転→画面位置→クリップ座標へ変換。
    // UVは切り出し範囲へ変換し、PSMainは画像の色にtintを掛ける。頂点バッファは使わない。
    // 文字列自体は変更せず、解説はここへ置く。
    constexpr char Shader[] = R"(
cbuffer Sprite : register(b0) {
    float4 rect;
    float4 tint;
    float4 uvRect;
    float4 rotation; // cos, sin, virtualWidth, virtualHeight
};
Texture2D image : register(t0);
SamplerState nearest : register(s0);
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOutput VSMain(uint id : SV_VertexID) {
    const float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    float2 corner = corners[id];
    float2 local = (corner - 0.5) * rect.zw;
    float2 pixel = rect.xy + rect.zw * 0.5 + float2(
        local.x * rotation.x - local.y * rotation.y,
        local.x * rotation.y + local.y * rotation.x);
    VSOutput o;
    o.position = float4(pixel.x / rotation.z * 2 - 1, 1 - pixel.y / rotation.w * 2, 0, 1);
    o.uv = uvRect.xy + corner * uvRect.zw;
    return o;
}
float4 PSMain(VSOutput i) : SV_Target { return image.Sample(nearest, i.uv) * tint; }
)";
}

// シェーダーの入力規約と、色をどう重ねるかを先に決める。
void SpriteRenderer::Initialize(ID3D12Device* device)
{
    using Microsoft::WRL::ComPtr;
    if (!device || pipeline_) throw std::logic_error("Invalid sprite renderer initialization.");
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    // float4が4個で16個の32ビット値。下のdata配列とHLSLのcbufferの順番を合わせる。
    parameters[0].Constants.Num32BitValues = 16;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // もう1つは画像SRVへのテーブル。画像の画素そのものを定数へコピーするわけではない。
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    // POINTは隣の画素と滑らかに混ぜない読み方。ドットの輪郭を残したい表示向け。
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    // 画像端を超えたUVは端の色に固定。World側の繰り返し表示とは設定が違う。
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = parameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> serialized, error;
    CheckGpu(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error));
    CheckGpu(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root_)));
    // VSMain/PSMainを実行時コンパイル。HLSLのエラー文も例外に含める。
    const auto compile = [](const char* entry, const char* target)
    {
        ComPtr<ID3DBlob> code, errors;
        const auto hr = D3DCompile(Shader, sizeof(Shader) - 1, "DESIReSprite", nullptr, nullptr,
            entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (FAILED(hr) && errors) throw std::runtime_error(std::string(
            static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        CheckGpu(hr);
        return code;
    };
    const auto vs = compile("VSMain", "vs_5_0");
    const auto ps = compile("PSMain", "ps_5_0");
    // 描画設定をまとめてPSOに焼く。画像ごとの座標や色はここでは固定しない。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.SampleMask = UINT_MAX;
    pso.SampleDesc.Count = 1;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    auto& blend = pso.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    // 普通のalpha合成。画像側の色×alpha + 既にある色×(1-alpha)で重ねる。
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    // 画面座標の画像は描画順で重ねる。3Dの深度描画はWorldRendererの別PSOが担当する。
    // Sprite同士は描いた順に重なる。Z深度で並べる仕組みではない。
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    CheckGpu(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline_)));
}

// 無効な依頼を検査し、描くものだけ順番に並べる。入力のspanを書き換えない。
void SpriteRenderer::Draw(ID3D12GraphicsCommandList* commands, std::span<const SpriteDraw> sprites,
    UINT targetWidth, UINT targetHeight, float canvasWidth, float canvasHeight) const
{
    if (!pipeline_ || !commands || !targetWidth || !targetHeight) throw std::logic_error("Invalid sprite draw.");
    if (!std::isfinite(canvasWidth) || !std::isfinite(canvasHeight) ||
        canvasWidth <= 0 || canvasHeight <= 0 || canvasWidth > 16384 || canvasHeight > 16384)
        throw std::invalid_argument("Invalid sprite canvas size.");
    std::vector<const SpriteDraw*> sorted;
    sorted.reserve(sprites.size());
    for (const auto& sprite : sprites)
    {
        // 画像なし・非表示なら省く。座標が画面外かどうかのCPUカリングはまだしていない。
        if (!sprite.texture || !sprite.render.visible) continue;
        if (!std::isfinite(sprite.x) || !std::isfinite(sprite.y) || !std::isfinite(sprite.width) ||
            !std::isfinite(sprite.height) || !std::isfinite(sprite.rotationDegrees) || sprite.width <= 0 || sprite.height <= 0)
            throw std::invalid_argument("Sprite coordinates must be finite and size must be positive.");
        for (float c : sprite.tint) if (!std::isfinite(c) || c < 0 || c > 1) throw std::invalid_argument("Invalid sprite tint.");
        for (float uv : sprite.uv) if (!std::isfinite(uv)) throw std::invalid_argument("Invalid sprite UV.");
        sorted.push_back(&sprite);
    }
    // layer→orderInLayerの昇順。同値なら入力順を保つので、先後が毎回ばらつかない。
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b)
    { return a->render.layer != b->render.layer ? a->render.layer < b->render.layer : a->render.orderInLayer < b->render.orderInLayer; });
    // アスペクト比を保つ。余白には背景色や先に描いたWorldが残る。入力座標の変換はUI実装時に共通化する。
    // 指定された座標系を縦横同じ倍率で中央配置。0,0での自動指定は上位Rendererが解決済み。
    const float scale = (std::min)(targetWidth / canvasWidth, targetHeight / canvasHeight);
    const float width = canvasWidth * scale, height = canvasHeight * scale;
    const D3D12_VIEWPORT viewport{ (targetWidth - width) * .5f, (targetHeight - height) * .5f, width, height, 0, 1 };
    // 切り取り範囲は窓全体。UI部品ごとのクリップやマウス座標変換はまだない。
    const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(targetWidth), static_cast<LONG>(targetHeight) };
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->SetGraphicsRootSignature(root_.Get());
    commands->SetPipelineState(pipeline_.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (const auto* s : sorted)
    {
        // 大きな角度を360度周期へ戻してから三角関数へ。回転の中心は矩形の中央。
        const float radians = std::remainder(s->rotationDegrees, 360.0f) * 0.017453292519943295f;
        // HLSLのrect/tint/uvRect/rotationの順に詰める。この並びはCPUとGPUの約束。
        const float data[16] = { s->x,s->y,s->width,s->height,
            s->tint[0],s->tint[1],s->tint[2],s->tint[3],
            s->uv[0],s->uv[1],s->uv[2],s->uv[3], std::cos(radians),std::sin(radians),canvasWidth,canvasHeight };
        // 現状は画像ごとにSRVヒープを切り替える。共通ヒープ化やまとめ描画は今後の最適化。
        ID3D12DescriptorHeap* heaps[] = { s->texture->srvHeap_.Get() };
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRoot32BitConstants(0, 16, data, 0);
        commands->SetGraphicsRootDescriptorTable(1, s->texture->srvHeap_->GetGPUDescriptorHandleForHeapStart());
        // 6頂点を1回描く。名前はInstancedだが、ここでは1枚ずつ発行している。
        commands->DrawInstanced(6, 1, 0, 0);
    }
}
