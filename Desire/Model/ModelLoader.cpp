#include "Model/ModelLoader.h"
#include "Model/ModelInternal.h"
#include "Model/SkinPose.h"
#include "Render/D3D12Renderer.h"
#include "Resource/TextureLoader.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace
{
    std::string Text(ufbx_string s) { return {s.data,s.length}; }
    std::filesystem::path Relative(std::filesystem::path path)
    {
        path = path.lexically_normal();
        if (path.empty() || path.has_root_path()) throw std::invalid_argument("Model assets need a relative path.");
        for (const auto& part : path) if (part == "..") throw std::invalid_argument("Model asset path leaves Assets.");
        return path;
    }
    float Number(double x)
    { const auto f = static_cast<float>(x); if (!std::isfinite(f)) throw std::runtime_error("FBX contains non-finite data."); return f; }
    DirectX::XMFLOAT3 Vector(ufbx_vec3 v) { return {Number(v.x),Number(v.y),Number(v.z)}; }
    DirectX::XMFLOAT4X4 Inverse(const DirectX::XMFLOAT4X4& m)
    {
        using namespace DirectX;
        XMVECTOR determinant; const auto inv=XMMatrixInverse(&determinant,XMLoadFloat4x4(&m));
        if (XMVectorGetX(determinant)==0 || XMMatrixIsNaN(inv) || XMMatrixIsInfinite(inv))
            throw std::runtime_error("FBX has a singular transform.");
        XMFLOAT4X4 result; XMStoreFloat4x4(&result,inv); return result;
    }
    Material ReadMaterial(const ufbx_material* source, const std::filesystem::path& modelPath,
        TextureLoader& textures, bool loadTextures)
    {
        Material result;
        if (!source) return result;
        const auto& color=source->pbr.base_color.has_value ? source->pbr.base_color : source->fbx.diffuse_color;
        if (color.has_value) result.color={std::clamp(Number(color.value_vec3.x),0.0f,1.0f),
            std::clamp(Number(color.value_vec3.y),0.0f,1.0f),std::clamp(Number(color.value_vec3.z),0.0f,1.0f)};
        const auto* texture=color.texture;
        if (loadTextures && texture && color.texture_enabled)
        {
            if (texture->type!=UFBX_TEXTURE_FILE || !texture->relative_filename.length)
                throw std::runtime_error("FBX texture needs an external relative image path; embedded/procedural textures are not supported.");
            // FBXの絶対パスへ勝手にアクセスしない。画像はモデルのフォルダからAssets内へ解決。
            const auto name=Text(texture->relative_filename);
            const std::filesystem::path relative(std::u8string(name.begin(),name.end()));
            if(relative.has_root_path()) throw std::runtime_error("FBX texture path must be relative.");
            result.texture=textures.Load(Relative(modelPath.parent_path()/relative));
        }
        return result;
    }
}
std::size_t ModelAsset::NodeCount() const noexcept { return data_ ? data_->scene->nodes.count : 0; }
std::size_t ModelAsset::ImportMemoryBytes() const noexcept { return data_ ? data_->memoryBytes : 0; }
void ModelLoader::PruneExpired() { std::erase_if(cache_,[](const auto& item){return item.second.expired();}); }
ModelHandle ModelLoader::Load(const std::filesystem::path& requested, bool loadTextures)
{
    using namespace DirectX;
    const auto path=Relative(requested);
    PruneExpired();
    const auto arena=memory_.GetArena(ArenaType::Scene);
    const auto operation=arena->BeginOperation();
    if (!operation) throw std::runtime_error("Cannot load a model into a closing arena.");
    const Key key{arena->GetId(),path,loadTextures};
    if (const auto found=cache_.find(key); found!=cache_.end()) if (auto asset=found->second.lock()) return asset;
    try
    {
        const auto source=resources_.AcquireBinary(path);
        const auto bytes=source->GetBytes(); // ufbxが読み終わるまで元バイト列の所有を保持。
        ufbx_load_opts options{};
        options.file_format=UFBX_FILE_FORMAT_FBX;
        options.target_axes=ufbx_axes_left_handed_y_up; options.target_unit_meters=1;
        options.space_conversion=UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
        options.geometry_transform_handling=UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
        options.inherit_mode_handling=UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
        options.generate_missing_normals=true; options.normalize_normals=true;
        options.load_external_files=false; options.node_depth_limit=128;
        options.temp_allocator.memory_limit=64*BlockAllocator::MiB;
        options.result_allocator.memory_limit=64*BlockAllocator::MiB;
        ufbx_error error{};
        auto model=std::make_shared<ModelAsset>(); model->data_=std::make_shared<ModelData>();
        auto& data=*model->data_;
        data.scene.reset(ufbx_load_memory(bytes.data(),bytes.size(),&options,&error));
        if (!data.scene) throw std::runtime_error(Text(error.description));
        const auto& scene=*data.scene;
        if (scene.nodes.count>2048 || scene.anim_stacks.count>64) throw std::runtime_error("FBX exceeds the node/clip limit.");
        data.memoryBytes=scene.metadata.result_memory_used;
        data.nodeOrder.resize(scene.nodes.count);
        std::iota(data.nodeOrder.begin(),data.nodeOrder.end(),0u);
        std::stable_sort(data.nodeOrder.begin(),data.nodeOrder.end(),[&](auto a,auto b){return scene.nodes[a]->node_depth<scene.nodes[b]->node_depth;});
        bool firstBounds=true;
        std::size_t triangleTotal=0;
        for (const auto* node : scene.nodes)
        {
            const auto* mesh=node->mesh;
            if (!mesh || !mesh->num_triangles) continue;
            if (mesh->skin_deformers.count>1 || mesh->blend_deformers.count || mesh->cache_deformers.count)
                throw std::runtime_error("Model08 supports one linear skin per mesh; morph/cache deformers are not supported.");
            const ufbx_skin_deformer* skin=mesh->skin_deformers.count ? mesh->skin_deformers[0] : nullptr;
            if (skin && skin->skinning_method!=UFBX_SKINNING_METHOD_LINEAR)
                throw std::runtime_error("Export with Linear skinning (disable Preserve Volume).");
            const auto clusterCount=skin ? skin->clusters.count : 0;
            if (clusterCount>=MaxSkinJoints) throw std::runtime_error("A mesh supports at most 127 skin joints plus its unweighted fallback.");
            const auto geometry=ModelMatrix(node->geometry_to_world);
            const auto geometryInverse=Inverse(geometry);
            const auto normalMatrix=XMMatrixTranspose(XMLoadFloat4x4(&geometryInverse));
            ModelSkin binding; binding.inverseGeometry=geometryInverse;
            for (std::size_t i=0;i<clusterCount;++i)
                binding.bindings.push_back({skin->clusters[i]->bone_node->typed_id,ModelMatrix(skin->clusters[i]->geometry_to_bone)});
            binding.bindings.push_back({node->typed_id,ModelMatrix(node->geometry_to_node)});
            // 材質ごとに分ける。位置やアニメ時計は後でModelInstanceがまとめて扱う。
            for (const auto& part : mesh->material_parts)
            {
                if (!part.num_triangles) continue;
                if (part.num_triangles>100000 || triangleTotal>100000-part.num_triangles || model->parts_.size()>=64)
                    throw std::runtime_error("FBX exceeds 100000 triangles or 64 material parts.");
                triangleTotal+=part.num_triangles;
                std::vector<MeshVertex> vertices; vertices.reserve(part.num_triangles*3);
                std::vector<std::uint32_t> indices; indices.reserve(part.num_triangles*3);
                std::vector<std::uint32_t> triangles(mesh->max_face_triangles*3);
                for (const auto faceIndex : part.face_indices)
                {
                    const auto count=ufbx_triangulate_face(triangles.data(),triangles.size(),mesh,mesh->faces[faceIndex]);
                    for (std::size_t i=0;i<std::size_t(count)*3;++i)
                    {
                        const auto corner=triangles[i];
                        MeshVertex v{}; const auto pos=Vector(mesh->vertex_position[corner]); const auto normal=Vector(mesh->vertex_normal[corner]);
                        XMStoreFloat3(&v.position,XMVector3TransformCoord(XMLoadFloat3(&pos),XMLoadFloat4x4(&geometry)));
                        XMStoreFloat3(&v.normal,XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&normal),normalMatrix)));
                        if (mesh->vertex_uv.exists) { const auto uv=mesh->vertex_uv[corner]; v.uv={Number(uv.x),1-Number(uv.y)}; }
                        double sum=0;
                        if (skin)
                        {
                            const auto weights=skin->vertices[mesh->vertex_indices[corner]];
                            const auto take=(std::min)(weights.num_weights,4u);
                            for (std::uint32_t j=0;j<take;++j)
                            {
                                const auto w=skin->weights[weights.weight_begin+j];
                                if (w.cluster_index>=clusterCount || !std::isfinite(w.weight) || w.weight<0) throw std::runtime_error("Invalid FBX skin weight.");
                                v.joints[j]=static_cast<std::uint16_t>(w.cluster_index); v.weights[j]=Number(w.weight); sum+=w.weight;
                            }
                        }
                        // ウェイト0の頂点は消さず、Meshノードの動きへ付いて行く。
                        if (sum<=1e-12) { v.joints={static_cast<std::uint16_t>(clusterCount),0,0,0}; v.weights={1,0,0,0}; }
                        else for (auto& w : v.weights) w=static_cast<float>(w/sum);
                        indices.push_back(static_cast<std::uint32_t>(vertices.size())); vertices.push_back(v);
                    }
                }
                const auto cpu=CpuMesh::Create(memory_,ArenaType::Scene,vertices,indices);
                ModelPart output; output.name=Text(node->name)+"/"+std::to_string(part.index);
                output.mesh=renderer_.UploadMesh(cpu);
                output.material=ReadMaterial(part.index<node->materials.count ? node->materials[part.index] : nullptr,path,textures_,loadTextures);
                const auto& bounds=cpu.Bounds();
                if (firstBounds) { model->bounds_=bounds; firstBounds=false; }
                else
                {
                    auto& b=model->bounds_;
                    b.min={ (std::min)(b.min.x,bounds.min.x),(std::min)(b.min.y,bounds.min.y),(std::min)(b.min.z,bounds.min.z) };
                    b.max={ (std::max)(b.max.x,bounds.max.x),(std::max)(b.max.y,bounds.max.y),(std::max)(b.max.z,bounds.max.z) };
                }
                model->parts_.push_back(std::move(output)); data.skins.push_back(binding);
            }
        }
        if (model->parts_.empty()) throw std::runtime_error("FBX has no drawable triangle mesh.");
        for (const auto* stack : scene.anim_stacks)
        {
            // Euler曲線などを60Hzで評価しておく。24fpsの元キーだけでQuaternion補間すると
            // 回転軸が複数ある動きで元のFBXとずれやすいため、低頻度のキーも再サンプルする。
            ufbx_bake_opts bakeOptions{}; bakeOptions.trim_start_time=true;
            bakeOptions.resample_rate=60; bakeOptions.minimum_sample_rate=60;
            bakeOptions.temp_allocator.memory_limit=32*BlockAllocator::MiB;
            bakeOptions.result_allocator.memory_limit=32*BlockAllocator::MiB;
            BakedAnimationPtr baked(ufbx_bake_anim(&scene,stack->anim,&bakeOptions,&error),ufbx_free_baked_anim);
            if (!baked) throw std::runtime_error(Text(error.description));
            const double duration=baked->playback_duration;
            if (!std::isfinite(duration) || duration<0 || duration>3600) throw std::runtime_error("Invalid or excessively long animation clip.");
            data.memoryBytes+=baked->metadata.result_memory_used;
            if (data.memoryBytes>128*BlockAllocator::MiB) throw std::runtime_error("FBX retained data exceeds 128 MiB.");
            model->clips_.push_back({Text(stack->name),duration}); data.animations.push_back(std::move(baked));
        }
        cache_[key]=model; return model;
    }
    catch (const std::exception& error) { throw std::runtime_error("FBX "+path.string()+": "+error.what()); }
}
