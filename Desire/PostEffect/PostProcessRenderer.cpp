#include "PostEffect/PostProcessRenderer.h"
#include "PostEffect/PostProcessShader.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

void PostProcessRenderer::Initialize(ID3D12Device* device)
{
    using Microsoft::WRL::ComPtr;
    if (!device || pipeline_) throw std::logic_error("Invalid post-process initialization.");
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 4;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = { 1, &range };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s0はNone用のPoint、s1はFast用のLinear。HLSLのregister番号と揃える。
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (auto& sampler : samplers)
    {
        // 画面の端を読んだ時、反対側の色を引っ張ってこないようにClampする。
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxAnisotropy = 1;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    samplers[0].ShaderRegister = 0;
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].ShaderRegister = 1;
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2; desc.pParameters = params;
    desc.NumStaticSamplers = 2; desc.pStaticSamplers = samplers;
    ComPtr<ID3DBlob> serialized, errors;
    CheckGpu(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
    CheckGpu(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root_)));
    const auto compile = [](const char* entry, const char* target)
    {
        ComPtr<ID3DBlob> code, error;
        const HRESULT hr = D3DCompile(PostProcessShader, sizeof(PostProcessShader) - 1, "DESIRePostProcess",
            nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &error);
        if (FAILED(hr) && error) throw std::runtime_error(std::string(
            static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize()));
        CheckGpu(hr);
        return code;
    };
    const auto vs = compile("VSMain", "vs_5_0");
    const auto ps = compile("PSMain", "ps_5_0");
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
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    CheckGpu(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline_)));
    const auto background = compile("BackgroundPS", "ps_5_0");
    pso.PS = { background->GetBufferPointer(), background->GetBufferSize() };
    CheckGpu(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&backgroundPipeline_)));
}

void PostProcessRenderer::FillBackground(ID3D12GraphicsCommandList* commands, UINT width, UINT height,
    const std::array<float, 4>& color) const
{
    if (!backgroundPipeline_ || !commands || !width || !height)
        throw std::logic_error("Invalid background draw.");
    for (float c : color) if (!std::isfinite(c) || c < 0 || c > 1)
        throw std::invalid_argument("Background color must be finite RGBA from zero to one.");
    const D3D12_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    const D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    commands->RSSetViewports(1,&viewport);
    commands->RSSetScissorRects(1,&scissor);
    commands->SetGraphicsRootSignature(root_.Get());
    commands->SetPipelineState(backgroundPipeline_.Get());
    commands->SetGraphicsRoot32BitConstants(0,4,color.data(),0);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3,1,0,0);
}

D3D12_VIEWPORT PostProcessRenderer::MakeViewport(UINT sw, UINT sh, UINT ww, UINT wh, UpscaleMode mode)
{
    if (!sw || !sh || !ww || !wh || sw > 16384 || sh > 16384 || ww > 16384 || wh > 16384 ||
        (mode != UpscaleMode::IntegerFit && mode != UpscaleMode::AspectFit))
        throw std::invalid_argument("Invalid presentation size or upscale mode.");
    float scale = (std::min)(static_cast<float>(ww) / sw, static_cast<float>(wh) / sh);
    if (mode == UpscaleMode::IntegerFit && scale >= 1) scale = std::floor(scale);
    const float width = sw * scale, height = sh * scale;
    // 余白が奇数なら片側を1px広くする。半端な座標で整数拡大を崩さない。
    const float x = std::floor((ww - width) * 0.5f), y = std::floor((wh - height) * 0.5f);
    return { x, y, width, height, 0, 1 };
}

void PostProcessRenderer::Draw(ID3D12GraphicsCommandList* commands, const InternalRenderTarget& source,
    UINT windowWidth, UINT windowHeight, const RendererSettings& settings) const
{
    if (!pipeline_ || !commands) throw std::logic_error("Post-process is not initialized.");
    const auto viewport = MakeViewport(source.Width(), source.Height(), windowWidth, windowHeight, settings.resolution.upscale);
    const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(windowWidth), static_cast<LONG>(windowHeight) };
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->SetPipelineState(pipeline_.Get());
    commands->SetGraphicsRootSignature(root_.Get());
    ID3D12DescriptorHeap* heaps[] = { source.SrvHeap() };
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetGraphicsRootDescriptorTable(1, source.SrvHeap()->GetGPUDescriptorHandleForHeapStart());
    const float data[] = { static_cast<float>(source.Height()), settings.crt.scanlineAlpha,
        settings.crt.vignetteStrength, settings.crt.quality == CrtQuality::Fast ? 1.0f : 0.0f };
    commands->SetGraphicsRoot32BitConstants(0, 4, data, 0);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
}
