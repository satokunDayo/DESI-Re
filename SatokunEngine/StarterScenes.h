#pragma once
// エンジンの使い方を示す確認用Sceneたち。Engine本体にTitleや迷路の事情を背負わせない。
#include "SceneManager.h"
#include "StateMachine.h"
#include "EventSequence.h"
#include "World.h"
#include "Camera.h"

// 標準の確認用ひな形。Engine本体はこのファイルをincludeしない。
// 最初の画面。移動先の名前2つと、表示用の画像を持つ。
class TitleScene final : public Scene
{
public:
    TitleScene(std::string start, std::string alternate);
    void OnEnter(SceneContext&) override;
    void OnExit(SceneContext&) noexcept override { image_.reset(); }
    void OnUpdate(SceneContext&, const SceneInput&, double) override;
    ScenePresentation GetPresentation() const override;
private:
    // 通常開始と別ステージの行き先。Sceneそのものへの生ポインタは保存しない。
    std::string start_;
    std::string alternate_;
    GpuTextureHandle image_;
};

// GameSceneの設定書。Stage01とStage02は同じクラスへ違う設定を渡して作る。
struct GameSceneSettings
{
    std::wstring caption = L"Game";
    std::array<float, 4> clearColor{ 0.08f, 0.18f, 0.32f, 1 };
    std::string titleScene = "Title";
    std::string resultScene = "Result";
    bool playIntro = true;
    // 横から平行投影で見るか。横スクロールの移動制約まで自動実装する旗ではない。
    bool sideView = false;
    std::vector<EventPage> eventPages;
};

// WorldとCameraを持つサンプル。Playing/Event/PausedはこのSceneの中で切り替える。
class GameScene final : public Scene
{
public:
    explicit GameScene(GameSceneSettings settings);
    void OnEnter(SceneContext&) override;
    void OnUpdate(SceneContext&, const SceneInput&, double) override;
    void OnFixedUpdate(SceneContext&, double) override;
    void OnExit(SceneContext&) noexcept override;
    ScenePresentation GetPresentation() const override;

    const std::string& GetMode() const noexcept { return modes_.GetCurrentId(); }
    double GetPlaySeconds() const noexcept;
    EventStatus GetEventStatus() const noexcept { return event_.GetStatus(); }

private:
    class PlayingState;
    class EventState;
    class PausedState;
    // SceneArenaへ置くゲーム中データの小さい例。HPや迷路のルールはまだ入っていない。
    struct RuntimeData
    {
        double playSeconds = 0.0;
        unsigned int finishedEvents = 0;
    };
    RuntimeData& Data() noexcept;
    const RuntimeData& Data() const noexcept;

    GameSceneSettings settings_;
    // 領域の返却担当。中のRuntimeDataの構築・破棄はOnEnter/OnExitで明示する。
    MemoryBlock runtime_;
    // 置いた物の持ち主。Scene退出時にWorldを消せば、持っている物も順に片付く。
    std::unique_ptr<World> world_;
    Camera camera_;
    // 回転させる対象はIDで覚える。削除やスロット再利用後に古いポインタを触らない。
    ObjectId rotatingObject_;
    GpuTextureHandle image_;
    EventSequence event_;
    SceneInput input_;
    // OnEnterでもらったサービスを借用。OnExitでnullにし、退出後には使わない。
    SceneContext* services_ = nullptr;
    // 最後に宣言し、先に破棄する。Exitで使う上のデータがまだ生きている。
    StateMachine<GameScene> modes_;
};

// 結果画面の最小例。今はEnter/Escで指定先へ戻るだけ。
class ResultScene final : public Scene
{
public:
    explicit ResultScene(std::string next) : next_(std::move(next)) {}
    void OnUpdate(SceneContext&, const SceneInput&, double) override;
    ScenePresentation GetPresentation() const override;
private:
    std::string next_;
};

// mainから呼ぶ登録関数。新しいSceneを増やす入口はここ。
void RegisterStarterScenes(SceneManager& scenes);
