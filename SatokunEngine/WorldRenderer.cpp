// 行列・簡単な光・材質をGPUへ渡す実装。MeshShader.hと対で読むと配線が見える。
#include "WorldRenderer.h"
#include "MeshShader.h"
#include <d3dcompiler.h>
#include <cstddef>
#include <stdexcept>

#pragma comment(lib,"d3dcompiler.lib")

// 描画中に毎回作らない設定をここでまとめて用意する。
void WorldRenderer::Initialize(ID3D12Device* device)
{
    using Microsoft::WRL::ComPtr;
    if(!device||depthPipeline_)throw std::logic_error("Invalid WorldRenderer initialization.");
    D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    // 16+16+16=48。WVP行列、法線行列、材質/光の定数という順番。
    parameters[0].Constants.Num32BitValues=48;parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges=1;parameters[1].DescriptorTable.pDescriptorRanges=&range;
    parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    // UVが0〜1を超えたら繰り返す。床の柄をタイル状に並べられる設定。
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=2;signature.pParameters=parameters;
    signature.NumStaticSamplers=1;signature.pStaticSamplers=&sampler;
    // 頂点バッファの入力レイアウトを使うRoot Signature。Spriteの頂点番号方式とは違う。
    signature.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized,errors;
    CheckGpu(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
    CheckGpu(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root_)));
    // HLSLをvs_5_0/ps_5_0へコンパイル。DX12でもこのShader Modelを利用できる。
    const auto compile=[](const char* entry,const char* target)
    {
        ComPtr<ID3DBlob> code,error;
        const auto hr=D3DCompile(MeshShaderSource,sizeof(MeshShaderSource)-1,"DESIReMesh",nullptr,nullptr,entry,target,
            D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(hr)&&error)throw std::runtime_error(std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()));
        CheckGpu(hr);return code;
    };
    const auto vs=compile("VSMain","vs_5_0"),ps=compile("PSMain","ps_5_0");
    // MeshVertexの各フィールド位置をoffsetofで指定。C++のデータとシェーダーのInputをつなぐ表。
    const D3D12_INPUT_ELEMENT_DESC layout[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(MeshVertex,position),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(MeshVertex,normal),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(MeshVertex,uv),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };
    // 三角形・RGBA8画面・D32深度・裏面カリングなし。最初の簡単な描画規則。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_.Get();
    pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.InputLayout={layout,3};pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.SampleMask=UINT_MAX;pso.SampleDesc.Count=1;pso.NumRenderTargets=1;
    pso.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable=TRUE;
    // BlendEnableはゼロ初期化のfalse。不透明/AlphaClip向けで、3D半透明の合成はしていない。
    auto& blend=pso.BlendState.RenderTarget[0];blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_ZERO;
    blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_ZERO;
    blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    auto& depth=pso.DepthStencilState;depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    // 既に書かれた深度より小さい、つまり手前のものだけ通す。同じ深度は通らない。
    depth.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    CheckGpu(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&depthPipeline_)));
    // 同じ設定を基に深度なしPSOも作る。こちらは描画した順番で重なる。
    depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    CheckGpu(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&orderedPipeline_)));
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors=1;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckGpu(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&nullTextureHeap_)));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels=1;
    // 画像がない場合のnull SRV。シェーダーはhasTextureを見てサンプルを省き、白を使う。
    device->CreateShaderResourceView(nullptr,&srv,nullTextureHeap_->GetCPUDescriptorHandleForHeapStart());
}

// Cameraの投影は共通、World行列と材質は物体ごと。共通計算をループ外へ出している。
void WorldRenderer::Draw(ID3D12GraphicsCommandList* commands,std::span<const RenderItem> items,
    const Camera& camera,float aspect) const
{
    using namespace DirectX;
    if(!commands||!depthPipeline_)throw std::logic_error("WorldRenderer is not initialized.");
    const auto viewProjection=camera.MakeViewProjection(aspect);
    commands->SetGraphicsRootSignature(root_.Get());
    commands->SetPipelineState(camera.IsDepthEnabled()?depthPipeline_.Get():orderedPipeline_.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(const auto& item:items)
    {
        const auto& visual=item.renderer;
        const auto& material=visual.material;
        const auto world=XMLoadFloat4x4(&item.world);
        XMVECTOR determinant;
        // 縦だけ伸ばすような倍率でも法線が正しく面に垂直であるよう、Worldの逆転置を使う。
        const auto normal=XMMatrixTranspose(XMMatrixInverse(&determinant,world));
        // 逆行列を作れないほど潰れた/極端な変換はここで止める。
        if(XMVectorGetX(determinant)==0||XMMatrixIsNaN(normal)||XMMatrixIsInfinite(normal))
            throw std::runtime_error("World matrix is not invertible at float precision.");
        XMFLOAT4X4 wvp,normalMatrix;
        // ローカル頂点をクリップ空間まで運ぶ行列。計算順はTransformとCameraの約束に合わせる。
        XMStoreFloat4x4(&wvp,world*XMLoadFloat4x4(&viewProjection));
        XMStoreFloat4x4(&normalMatrix,normal);
        // 色、固定方向光、UV変換、透過設定。光の方向(.3,1,-.5)と環境光.25は今はコード固定。
        const float values[16]={material.color.x,material.color.y,material.color.z,1,
            .3f,1,-.5f,material.lit?1.0f:0.0f,
            material.uvScale.x,material.uvScale.y,material.uvOffset.x,material.uvOffset.y,
            material.surface==SurfaceMode::AlphaClip?1.0f:0.0f,material.alphaCutoff,.25f,material.texture?1.0f:0.0f};
        // 画像ありならそのSRV、なければnull SRVを選ぶ。画像本体の所有は上位Rendererが保持済み。
        auto* heap=material.texture?material.texture->srvHeap_.Get():nullTextureHeap_.Get();
        commands->SetDescriptorHeaps(1,&heap);
        // 48個の定数を0/16/32の位置へ分けて設定。HLSLのcbufferと順番がずれると壊れる。
        commands->SetGraphicsRoot32BitConstants(0,16,&wvp,0);
        commands->SetGraphicsRoot32BitConstants(0,16,&normalMatrix,16);
        commands->SetGraphicsRoot32BitConstants(0,16,values,32);
        commands->SetGraphicsRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());
        // 頂点の実体と番号列をセット。形がBoxか外部データかを描画側は気にしない。
        commands->IASetVertexBuffers(0,1,&visual.mesh->vertexView_);
        commands->IASetIndexBuffer(&visual.mesh->indexView_);
        // インデックスを使って1個描く。複数オブジェクトのまとめ描画はまだしていない。
        commands->DrawIndexedInstanced(visual.mesh->indexCount_,1,0,0,0);
    }
}
