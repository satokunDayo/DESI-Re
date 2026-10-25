#include "Model/Animator.h"
#include "Model/ModelInternal.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

Animator::Animator(ModelHandle model) : model_(std::move(model))
{
    if (!model_ || !model_->data_) throw std::invalid_argument("Animator needs a loaded model.");
    globals_.resize(model_->NodeCount());
    for (const auto& skin : model_->data_->skins)
    { auto pose=std::make_shared<SkinPose>(); pose->count=skin.bindings.size(); poses_.push_back(std::move(pose)); }
    Evaluate();
}
void Animator::Play(std::size_t clip, bool loop)
{
    if (clip>=model_->Clips().size()) throw std::out_of_range("Animation clip index.");
    clip_=clip; time_=0; loop_=loop; paused_=false; playing_=true; Evaluate();
}
void Animator::Play(std::string_view name, bool loop)
{
    std::optional<std::size_t> found;
    for(std::size_t i=0;i<model_->Clips().size();++i) if(model_->Clips()[i].name==name)
    { if(found) throw std::invalid_argument("Duplicate clip name; use its index."); found=i; }
    if(!found) throw std::invalid_argument("Unknown animation clip: "+std::string(name));
    Play(*found,loop);
}
void Animator::Stop() { clip_.reset(); time_=0; playing_=paused_=false; Evaluate(); }
void Animator::Seek(double seconds)
{
    if (!clip_ || !std::isfinite(seconds) || seconds<0) throw std::invalid_argument("Seek needs a selected clip and non-negative finite time.");
    time_=(std::min)(seconds,model_->Clips()[*clip_].duration); Evaluate();
}
void Animator::Update(double seconds)
{
    if (!std::isfinite(seconds) || seconds<0) throw std::invalid_argument("Animation delta must be finite and non-negative.");
    if (!IsPlaying() || !clip_) return;
    const auto duration=model_->Clips()[*clip_].duration;
    if (duration<=0) { time_=0; playing_=false; }
    else if (loop_) time_=std::fmod(time_+std::fmod(seconds,duration),duration);
    else { time_+=(std::min)(seconds,duration-time_); if(time_>=duration) playing_=false; }
    Evaluate();
}
void Animator::Evaluate()
{
    using namespace DirectX;
    const auto& data=*model_->data_; const auto& scene=*data.scene;
    // ベイクされたキーを評価し、親から子へ行列を積む。FBX全体を毎フレーム解析し直さない。
    auto* animation=clip_ ? data.animations[*clip_].get() : nullptr;
    // trim_start_timeでキーは0始まり。元FBXのplayback_time_beginを再加算すると別の時刻になる。
    const double sampleTime=time_;
    for (auto index : data.nodeOrder)
    {
        const auto* node=scene.nodes[index]; auto transform=node->local_transform;
        if (animation)
            if (const auto* track=ufbx_find_baked_node_by_typed_id(animation,index))
            {
                if(track->translation_keys.count) transform.translation=ufbx_evaluate_baked_vec3(track->translation_keys,sampleTime);
                if(track->rotation_keys.count) transform.rotation=ufbx_evaluate_baked_quat(track->rotation_keys,sampleTime);
                if(track->scale_keys.count) transform.scale=ufbx_evaluate_baked_vec3(track->scale_keys,sampleTime);
            }
        auto m=ModelMatrix(ufbx_transform_to_matrix(&transform));
        auto global=XMLoadFloat4x4(&m);
        if (node->parent) global=global*XMLoadFloat4x4(&globals_[node->parent->typed_id]);
        if (XMMatrixIsNaN(global)||XMMatrixIsInfinite(global)) throw std::runtime_error("Animated transform overflow.");
        XMStoreFloat4x4(&globals_[index],global);
    }
    for (std::size_t p=0;p<data.skins.size();++p)
    {
        const auto& skin=data.skins[p]; auto& pose=*poses_[p];
        for (std::size_t j=0;j<skin.bindings.size();++j)
        {
            const auto& binding=skin.bindings[j];
            const auto m=XMLoadFloat4x4(&skin.inverseGeometry)*XMLoadFloat4x4(&binding.geometryToBone)*XMLoadFloat4x4(&globals_[binding.node]);
            XMVECTOR determinant; const auto normal=XMMatrixTranspose(XMMatrixInverse(&determinant,m));
            if (XMVectorGetX(determinant)==0 || XMMatrixIsNaN(normal) || XMMatrixIsInfinite(normal))
                throw std::runtime_error("Animation produced a singular skin matrix (zero scale is unsupported).");
            XMStoreFloat4x4(&pose.matrices[j].position,m); XMStoreFloat4x4(&pose.matrices[j].normal,normal);
        }
    }
}
