#pragma once
#include "Scene/Scene.h"
#include "Game/EnemyCharacter.h"
#include "World/World.h"
#include "World/MapData.h"
#include "Camera/CameraControllers.h"
#include "Physics/CharacterMotor.h"
#include <filesystem>
#include <memory>
#include <string>

struct MazeStageSettings
{
    int width = 37;
    int height = 37;
    int extraHoles = 3;
    // Assets基準。空なら保存しない。入場時に生成結果を上書き保存する。
    std::filesystem::path saveFile = "Maps/generated-stage01.map";
    std::string nextScene;     // ゴール後の行き先。今回は"Ending"。
    std::string gameOverScene; // 敵に触れた後の行き先。今回は"GameOver"。
    // 行き先が空なら迷路を止めてEnterで終了。PNGを表示するにはmainで登録・指定する。
};

// 名前はStage1Sceneのままだが、設定を替えてStage2にも登録できる。
class Stage1Scene final : public Scene
{
public:
    explicit Stage1Scene(MazeStageSettings settings = {});
    void OnEnter(SceneContext& context) override;
    void OnUpdate(SceneContext& context, const SceneInput& input, double seconds) override;
    void OnFixedUpdate(SceneContext& context, double seconds) override;
    void OnExit(SceneContext& context) noexcept override;
    ScenePresentation GetPresentation() const override;

private:
    // 勝敗が決まった後は、移動・AI・当たり判定を進めない。
    enum class RoundState { Playing, Clear, GameOver };
    void ResetRound(); // 同じ迷路内でプレイヤーと敵を戻す。生成・ロードはしない。

    MazeStageSettings settings_;
    MapGrid grid_; // 後で敵の配置候補を探せるよう、地図は残す。
    std::unique_ptr<World> world_;
    std::unique_ptr<CollisionWorld> collision_;
    std::unique_ptr<EnemyCharacter> enemy_; // Worldより先に片付ける。
    ObjectId player_, goal_;
    DirectX::XMFLOAT3 startPosition_{};
    Camera camera_;
    FirstPersonCamera cameraControl_;
    CharacterMotor motor_{ 2.5f, MovementPlane::XZ };
    DirectX::XMFLOAT3 moveDirection_{};
    RoundState roundState_ = RoundState::Playing;
};
