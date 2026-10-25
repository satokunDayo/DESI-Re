#pragma once
// 1体ぶんの再生時計と現在姿勢。ModelAssetを共有しても、このインスタンスは各キャラに持つ。
#include "Model/ModelAsset.h"
#include "Model/SkinPose.h"
#include <optional>
#include <string_view>
class Animator final
{
public:
    explicit Animator(ModelHandle model);
    Animator(const Animator&) = delete;
    Animator& operator=(const Animator&) = delete;
    void Play(std::size_t clip, bool loop = true);
    void Play(std::string_view name, bool loop = true);
    void Pause(bool paused = true) noexcept { paused_ = paused; }
    void Stop(); // 再生を止めてFBXの既定姿勢へ戻す。
    void Seek(double seconds);
    void Update(double seconds);
    bool IsPlaying() const noexcept { return playing_ && !paused_; }
    double Time() const noexcept { return time_; }
    std::optional<std::size_t> CurrentClip() const noexcept { return clip_; }
    SkinPoseHandle Pose(std::size_t part) const { return poses_.at(part); }
    const ModelHandle& Model() const noexcept { return model_; }
private:
    void Evaluate();
    ModelHandle model_;
    std::optional<std::size_t> clip_;
    double time_ = 0;
    bool playing_ = false, paused_ = false, loop_ = true;
    std::vector<DirectX::XMFLOAT4X4> globals_;
    std::vector<std::shared_ptr<SkinPose>> poses_;
};
