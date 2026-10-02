#pragma once
// Sceneの名簿と現在のSceneを持つ。切替時はGPUとCPUの借り物が片付く順番まで面倒を見る。
#include "Scene.h"
#include "MemorySystem/Manager/MemoryManager.h"
#include "ResourceManager.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

// Readyなら更新できる。WaitingForReleaseは旧Sceneの返却待ちで、非同期ロード完成の印ではない。
enum class SceneStatus { Empty, Ready, WaitingForRelease, Failed, Stopped };

// 更新・登録・切替はメインスレッド専用。登録時は設定と生成関数だけを保存。
class SceneManager final
{
public:
    // 名前が選ばれたときに新しいSceneを作る関数。登録時に全ステージを生成しておかない。
    using Factory = std::function<std::unique_ptr<Scene>()>;
    SceneManager(MemoryManager& memory, ResourceManager& resources,
        std::function<void()> waitForGpu = {}, TextureLoader* textures = nullptr, MeshLoader* meshes = nullptr);
    ~SceneManager();
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void RegisterFactory(std::string name, Factory factory);
    template<class T, class... Args>
    // 設定をコピーして保存し、再入場のたびに新しいTを作れるようにする登録口。
    void Register(std::string name, Args&&... args)
    {
        // Scene以外を登録しようとしたらコンパイル時に止める。型の交通整理。
        static_assert(std::is_base_of_v<Scene, T>);
        static_assert((std::is_copy_constructible_v<std::decay_t<Args>> && ...));
        // 再入場時にも設定から新しいSceneを作る。設定は値で渡す。
        // ポインタ/参照ラッパーを設定に含める場合、その参照先の寿命は呼出側が守る。
        // 引数をtupleへまとめる。参照先を含めるなら、その寿命は呼び出し側の責任。
        auto settings = std::make_tuple(std::forward<Args>(args)...);
        RegisterFactory(std::move(name), [settings = std::move(settings)]
        {
            return std::apply([](const auto&... values) -> std::unique_ptr<Scene>
            {
                return std::make_unique<T>(values...);
            }, settings);
        });
    }
    bool Remove(std::string_view name);
    // ここは予約受付。呼んだ瞬間に実行中のSceneを破壊しない。
    bool RequestChange(std::string_view name);
    void RequestQuit() noexcept { quitRequested_ = true; }
    bool IsQuitRequested() const noexcept { return quitRequested_; }

    void Update(const SceneInput& input, double seconds);
    void FixedUpdate(double seconds);
    void Shutdown() noexcept;
    ScenePresentation GetPresentation() const;

    SceneStatus GetStatus() const noexcept { return status_; }
    const std::string& GetCurrentName() const noexcept { return currentName_; }
    const std::string& GetLastError() const noexcept { return error_; }
    bool IsTransitionPending() const noexcept { return pending_.has_value(); }

private:
    void BeginChange();
    void TryEnterNext();
    void RetireCurrent();
    void Fail(const char* message);
    static void ValidateDelta(double seconds);

    // Sceneが借りるサービス一覧。参照先のEngine側はこのManagerより長生きさせる。
    SceneContext context_;
    // DX12の型を直に持たず「GPUを待って」という仕事だけ頼めるようにしている。
    std::function<void()> waitForGpu_;
    // シーン名から作り方を引く名簿。current_が今いるSceneの実際の持ち主。
    std::map<std::string, Factory, std::less<>> factories_;
    std::unique_ptr<Scene> current_;
    std::string currentName_;
    // pendingは切替予約、nextNameは解放待ちの後に入る名前。受付と待機先を混ぜない。
    std::optional<std::string> pending_;
    std::string nextName_;
    std::string error_;
    SceneStatus status_ = SceneStatus::Empty;
    // コールバック中の再帰操作を防ぐ旗。mutexではなくメインスレッド内の順序チェック。
    bool busy_ = false;
    // 退出中に次々と切替を予約されると片付けが迷子になるので、その間は受付を止める。
    bool retiring_ = false;
    bool quitRequested_ = false;
};
