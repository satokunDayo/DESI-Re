#pragma once
// 共有モデルから「この1体」を作る。配置・当たり判定は根元、材質別の描画物は子として同期。
#include "Model/Animator.h"
#include "World/World.h"
class ModelInstance final
{
public:
    // Worldが実体を所有する。根元のmeshRendererは空にして渡す。AutoFitはモデル全体へ解決。
    ModelInstance(World& world, ModelHandle model, ObjectDesc root = {});
    ModelInstance(const ModelInstance&) = delete;
    ModelInstance& operator=(const ModelInstance&) = delete;
    ObjectId Root() const noexcept { return root_; }
    std::span<const ObjectId> Parts() const noexcept { return parts_; }
    Animator& Animation() noexcept { return animation_; }
    const Animator& Animation() const noexcept { return animation_; }
    // 移動処理の後、描画の前に1回。根元が削除されたら残りも削除予約してfalseを返す。
    bool Update(World& world, double seconds);
    void SetVisible(World& world, bool visible);
    void RequestDestroy(World& world) noexcept;
    // デストラクタはWorldへ触れない。Scene終了ならWorldごと破棄し、途中退場ならRequestDestroy。
private:
    Animator animation_;
    ObjectId root_;
    std::vector<ObjectId> parts_;
};
