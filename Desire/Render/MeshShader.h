#pragma once
// 3D形状用のHLSLをC++文字列として持つ。中身はGPUで動く別言語。
// cbufferはCPUから渡す定数の受け皿。行列2個+float4が4個で48個の32ビット値。
// row_majorは行列のメモリ配置をCPU側とそろえる指定。位置は頂点×World×View×Projection。
// VSMainは位置を画面向けに変換し、法線へ逆転置行列を掛け、UVへ倍率とずらしを適用する。
// 法線のw=0は方向として扱うため。位置のように平行移動させない。
// PSMainは画像ありならサンプル、なければ白。AlphaClipならしきい値未満の画素を捨てる。
// 光への方向と法線の内積で明るさを計算。裏向きでもambient分は残し、lit=falseなら明るさ1。
// 最後は画像RGB×材質RGB×明るさ、alpha=1。不透明/穴抜き用で、半透明や影はまだない。ボーンはVSで先に変形する。
// BoneDataの128本はSkinPose.hのMaxSkinJointsとそろえる。tint.wはスキン有効フラグ。
inline constexpr char MeshShaderSource[]=R"(
cbuffer ObjectData : register(b0) {
    row_major float4x4 worldViewProjection;
    row_major float4x4 normalMatrix;
    float4 tint;
    float4 light; // 光へ向かう方向xyz、照明有効w
    float4 uvTransform;
    float4 options; // alphaClip, cutoff, ambient, hasTexture
};
struct Bone { row_major float4x4 position; row_major float4x4 normal; };
cbuffer BoneData : register(b1) { Bone bones[128]; };
Texture2D image : register(t0);
SamplerState nearestWrap : register(s0);
struct Input { float3 position:POSITION; float3 normal:NORMAL; float2 uv:TEXCOORD0; uint4 joints:BLENDINDICES; float4 weights:BLENDWEIGHT; };
struct Output { float4 position:SV_Position; float3 normal:NORMAL; float2 uv:TEXCOORD0; };
Output VSMain(Input i) {
    Output o;
    float4 p=float4(i.position,1), n=float4(i.normal,0);
    if(tint.w>0.5) {
        p=0; n=0;
        [unroll] for(uint j=0;j<4;++j) {
            p+=mul(float4(i.position,1),bones[i.joints[j]].position)*i.weights[j];
            n+=mul(float4(i.normal,0),bones[i.joints[j]].normal)*i.weights[j];
        }
    }
    o.position=mul(p,worldViewProjection);
    o.normal=mul(float4(n.xyz,0),normalMatrix).xyz;
    o.uv=i.uv*uvTransform.xy+uvTransform.zw;
    return o;
}
float4 PSMain(Output i):SV_Target {
    float4 texel=options.w>0.5?image.Sample(nearestWrap,i.uv):float4(1,1,1,1);
    if(options.x>0.5) clip(texel.a-options.y);
    float diffuse=max(dot(normalize(i.normal),normalize(light.xyz)),0);
    float brightness=light.w>0.5?options.z+(1-options.z)*diffuse:1;
    return float4(texel.rgb*tint.rgb*brightness,1);
}
)";
