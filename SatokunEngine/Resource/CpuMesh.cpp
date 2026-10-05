// 形状の検査・CPUコピー・箱と板の作成。描画命令を出す場所ではない。
#include "Resource/CpuMesh.h"
#include "Model/SkinPose.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

// インデックス3個で三角形1枚。空の配列や3で割り切れない番号列は拒否する。
CpuMesh CpuMesh::Create(MemoryManager& memory, ArenaType scope,
    std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices)
{
    if(vertices.empty() || indices.empty() || indices.size()%3 ||
        vertices.size_bytes()>BlockAllocator::MaxAllocationSize || indices.size_bytes()>BlockAllocator::MaxAllocationSize)
        throw std::invalid_argument("Mesh needs bounded vertex/index arrays and triangle indices.");
    CpuMesh result;
    // 最初の頂点から境界を始め、各軸の最小/最大へ広げる。
    result.bounds_={vertices.front().position,vertices.front().position};
    for(const auto& v:vertices)
    {
        for(float value:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y})
            if(!std::isfinite(value)) throw std::invalid_argument("Mesh data must be finite.");
        // 法線がゼロや極端な長さだと正規化できない。ここで異常な形状を止める。
        const double n=double(v.normal.x)*v.normal.x+double(v.normal.y)*v.normal.y+double(v.normal.z)*v.normal.z;
        if(n<1e-12 || n>1e12) throw std::invalid_argument("Mesh normal is degenerate.");
        float weightSum=0;
        for(std::size_t j=0;j<4;++j)
        {
            if(!std::isfinite(v.weights[j]) || v.weights[j]<0 || v.joints[j]>=MaxSkinJoints)
                throw std::invalid_argument("Invalid mesh skin weight or joint.");
            weightSum+=v.weights[j];
            if(v.weights[j]>0) result.requiredSkinJoints_=(std::max)(result.requiredSkinJoints_,std::size_t(v.joints[j])+1);
        }
        if(weightSum!=0 && std::abs(weightSum-1)>1e-4f) throw std::invalid_argument("Skin weights must add up to one.");
        auto& lo=result.bounds_.min;auto& hi=result.bounds_.max;
        lo.x=(std::min)(lo.x,v.position.x);lo.y=(std::min)(lo.y,v.position.y);lo.z=(std::min)(lo.z,v.position.z);
        hi.x=(std::max)(hi.x,v.position.x);hi.y=(std::max)(hi.y,v.position.y);hi.z=(std::max)(hi.z,v.position.z);
    }
    // 同じMeshで静的頂点とスキン頂点を混ぜない。重みなしはインポータ側で親ノードへ割り当てる。
    if(result.requiredSkinJoints_) for(const auto& v:vertices)
        if(v.weights[0]+v.weights[1]+v.weights[2]+v.weights[3]==0)
            throw std::invalid_argument("A skinned mesh cannot contain zero-weight vertices.");
    // 番号は頂点配列の添字。範囲外をGPUへ渡さないため、コピー前に全部確認する。
    for(auto index:indices) if(index>=vertices.size()) throw std::invalid_argument("Mesh index is out of range.");
    // 型ごとのアラインメントを維持して確保する。片方だけ成功してもRAIIで返せる。
    result.vertices_=memory.Allocate(scope,vertices.size_bytes(),alignof(MeshVertex));
    result.indices_=memory.Allocate(scope,indices.size_bytes(),alignof(std::uint32_t));
    if(!result.vertices_ || !result.indices_)throw std::bad_alloc();
    // この頂点型は数値データなのでバイトコピーできる。任意のクラスへ同じことをしてよいわけではない。
    std::memcpy(result.vertices_.Data(),vertices.data(),vertices.size_bytes());
    std::memcpy(result.indices_.Data(),indices.data(),indices.size_bytes());
    result.vertexCount_=vertices.size();result.indexCount_=indices.size();
    return result;
}
// 所有しているバイト列を頂点として読む窓口。ここで新しい配列を作らない。
std::span<const MeshVertex> CpuMesh::Vertices() const noexcept
{return {static_cast<const MeshVertex*>(vertices_.Data()),vertexCount_};}
// インデックス数はバイト数ではない。転送時のサイズはspanのsize_bytesで求める。
std::span<const std::uint32_t> CpuMesh::Indices() const noexcept
{return {static_cast<const std::uint32_t*>(indices_.Data()),indexCount_};}

// 幅1・高さ1・奥行き1の箱と、幅1・高さ1の板を作る。実際のサイズはTransformで変える。
CpuMesh CpuMesh::MakePrimitive(MemoryManager& memory,ArenaType scope,PrimitiveShape shape)
{
    // 箱は8頂点でなく24頂点。角の位置が同じでも、面ごとの法線とUVが違うので分けている。
    const MeshVertex vertices[]={
        {{-.5f,-.5f,-.5f},{0,0,-1},{0,1}},{{-.5f,.5f,-.5f},{0,0,-1},{0,0}},
        {{.5f,.5f,-.5f},{0,0,-1},{1,0}},{{.5f,-.5f,-.5f},{0,0,-1},{1,1}},
        {{.5f,-.5f,.5f},{0,0,1},{0,1}},{{.5f,.5f,.5f},{0,0,1},{0,0}},
        {{-.5f,.5f,.5f},{0,0,1},{1,0}},{{-.5f,-.5f,.5f},{0,0,1},{1,1}},
        {{-.5f,-.5f,.5f},{-1,0,0},{0,1}},{{-.5f,.5f,.5f},{-1,0,0},{0,0}},
        {{-.5f,.5f,-.5f},{-1,0,0},{1,0}},{{-.5f,-.5f,-.5f},{-1,0,0},{1,1}},
        {{.5f,-.5f,-.5f},{1,0,0},{0,1}},{{.5f,.5f,-.5f},{1,0,0},{0,0}},
        {{.5f,.5f,.5f},{1,0,0},{1,0}},{{.5f,-.5f,.5f},{1,0,0},{1,1}},
        {{-.5f,.5f,-.5f},{0,1,0},{0,1}},{{-.5f,.5f,.5f},{0,1,0},{0,0}},
        {{.5f,.5f,.5f},{0,1,0},{1,0}},{{.5f,.5f,-.5f},{0,1,0},{1,1}},
        {{-.5f,-.5f,.5f},{0,-1,0},{0,1}},{{-.5f,-.5f,-.5f},{0,-1,0},{0,0}},
        {{.5f,-.5f,-.5f},{0,-1,0},{1,0}},{{.5f,-.5f,.5f},{0,-1,0},{1,1}}
    };
    // 6面×2三角形×3頂点=36個。頂点本体を何度も複製せず番号で参照する。
    const std::uint32_t indices[]={0,1,2,0,2,3,4,5,6,4,6,7,8,9,10,8,10,11,
        12,13,14,12,14,15,16,17,18,16,18,19,20,21,22,20,22,23};
    if(shape==PrimitiveShape::Box)return Create(memory,scope,vertices,indices);
    if(shape!=PrimitiveShape::Quad)throw std::invalid_argument("Unknown primitive.");
    // 板はXY平面のZ=0に置き、法線は-Z。壁や床にしたい向きへはTransformで回す。
    const MeshVertex quad[]={{{-.5f,-.5f,0},{0,0,-1},{0,1}},{{-.5f,.5f,0},{0,0,-1},{0,0}},
        {{.5f,.5f,0},{0,0,-1},{1,0}},{{.5f,-.5f,0},{0,0,-1},{1,1}}};
    return Create(memory,scope,quad,std::span(indices,6));
}
