// シーン切替の引っ越し手順。旧住人が荷物を持ったままなら、新居へ急がず返却を待つ。
#include "SceneManager.h"
#include <cmath>
#include <stdexcept>

namespace
{
    // 処理中フラグをスコープで戻す。例外で抜けてもbusyだけ置き去りにしない。
    struct CallbackGuard
    {
        explicit CallbackGuard(bool& busy) : busy_(busy)
        {
            if (busy_) throw std::logic_error("Recursive SceneManager operation.");
            busy_ = true;
        }
        ~CallbackGuard() { busy_ = false; }
        bool& busy_;
    };
}

SceneManager::SceneManager(MemoryManager& memory, ResourceManager& resources,
    std::function<void()> waitForGpu, TextureLoader* textures, MeshLoader* meshes)
    : context_{ memory, resources, *this, textures, meshes }, waitForGpu_(std::move(waitForGpu)) {}

SceneManager::~SceneManager() { Shutdown(); }

// 重複名や空の工場は登録しない。更新コールバックの最中に名簿を書き換えるのも禁止。
void SceneManager::RegisterFactory(std::string name, Factory factory)
{
    if (busy_ || status_ == SceneStatus::Stopped)
        throw std::logic_error("Cannot register scenes now.");
    if (name.empty() || !factory || factories_.contains(name))
        throw std::invalid_argument("Empty or duplicate scene name: " + name);
    factories_.emplace(std::move(name), std::move(factory));
}

// 現在使っているSceneや移動予定の名前を消すと、あとで行き先がなくなるので拒否。
bool SceneManager::Remove(std::string_view name)
{
    if (busy_ || name == currentName_ || name == nextName_ ||
        (pending_ && name == *pending_)) return false;
    const auto found = factories_.find(name);
    if (found == factories_.end()) return false;
    factories_.erase(found);
    return true;
}

// 同じ確定区間では先着1件。2件目が来ても先の予約をこっそり上書きしない。
bool SceneManager::RequestChange(std::string_view name)
{
    if (retiring_ || status_ == SceneStatus::Stopped) return false;
    if (!factories_.contains(name))
        throw std::invalid_argument("Unknown scene: " + std::string(name));
    if (pending_ || status_ == SceneStatus::WaitingForRelease) return false;
    pending_ = std::string(name);
    return true;
}

// 負の時間やNaNを受け入れると、待ちやタイマーの判断が壊れる。入口で止める。
void SceneManager::ValidateDelta(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        throw std::invalid_argument("Invalid scene delta time.");
}

// 解放待ち→切替予約→現在のScene更新、の順で処理する。
void SceneManager::Update(const SceneInput& input, double seconds)
{
    ValidateDelta(seconds);
    CallbackGuard guard(busy_);
    if (status_ == SceneStatus::Stopped) return;
    if (status_ != SceneStatus::Ready && input.cancel) quitRequested_ = true;
    if (quitRequested_) return;
    try
    {
        if (status_ == SceneStatus::WaitingForRelease)
        {
            // ここで入場できても、そのフレームの決定入力を新Sceneへ使い回さない。
            TryEnterNext();
            return; // 入場フレームの入力を新シーンへ使い回さない。
        }
        if (pending_) { BeginChange(); return; }
        if (current_ && status_ == SceneStatus::Ready)
            current_->OnUpdate(context_, input, seconds);
        if (pending_ && !quitRequested_) BeginChange();
    }
    catch (const std::exception& error) { Fail(error.what()); }
    catch (...) { Fail("Unknown exception in scene update."); }
}

// ReadyなSceneだけ更新する。引っ越し予約がある旧Sceneをもう1歩進めない。
void SceneManager::FixedUpdate(double seconds)
{
    ValidateDelta(seconds);
    CallbackGuard guard(busy_);
    if (status_ != SceneStatus::Ready || pending_ || quitRequested_) return;
    try { current_->OnFixedUpdate(context_, seconds); }
    catch (const std::exception& error) { Fail(error.what()); }
    catch (...) { Fail("Unknown exception in scene fixed update."); }
}

// GPUが参照し終わってからOnExit。画像・メッシュを先に捨てる順番にはしない。
void SceneManager::RetireCurrent()
{
    retiring_ = true;
    // GPUの所有物をOnExit/デストラクタで返す前に待機する。
    try { if (waitForGpu_) waitForGpu_(); }
    catch (...) { retiring_ = false; throw; }
    if (current_) current_->OnExit(context_);
    // OnExitが済んだScene本体を破棄。unique_ptrなのでdeleteの書き忘れは起きない。
    current_.reset();
    currentName_.clear();
    try { context_.resources.BeginSceneClose(); }
    catch (...) { retiring_ = false; throw; }
    retiring_ = false;
}

// 予約を待機先へ移し、旧Sceneを退出させる。新Sceneの生成は次回以降へ。
void SceneManager::BeginChange()
{
    nextName_ = std::move(*pending_);
    pending_.reset();
    error_.clear();
    RetireCurrent();
    status_ = SceneStatus::WaitingForRelease;
    // 最低1フレームはLoadingを表示。生きたハンドルがあれば次回以降も待つ。
}

// CPUブロックやPoolの貸出が残っていればfalseのまま待つ。生きたメモリを踏み潰さない。
void SceneManager::TryEnterNext()
{
    if (!context_.memory.TryResetScene()) return;
    const auto target = nextName_;
    // 空っぽの新しいSceneArenaを用意できてからSceneを生成し、OnEnterへ進む。
    auto next = factories_.at(target)();
    if (!next) throw std::runtime_error("Scene factory returned null: " + target);
    current_ = std::move(next);
    currentName_ = target;
    nextName_.clear();
    status_ = SceneStatus::Ready;
    current_->OnEnter(context_);
    // OnEnter中の切替要求は、次のUpdateまで予約として保持する。
}

// Scene内の例外を受けたときの片付け。壊れた状態で通常更新を続けない。
void SceneManager::Fail(const char* message)
{
    error_ = message;
    pending_.reset();
    nextName_.clear();
    try { RetireCurrent(); }
    catch (...)
    {
        // GPUエラー等で安全な待機に失敗した場合、通常処理を再開しない。
        quitRequested_ = true;
    }
    status_ = SceneStatus::Failed;
}

// 明示終了とデストラクタの両方から呼べるよう、Stoppedなら何もしない。
void SceneManager::Shutdown() noexcept
{
    if (busy_ || status_ == SceneStatus::Stopped) return;
    busy_ = true;
    pending_.reset();
    nextName_.clear();
    try { RetireCurrent(); }
    catch (...)
    {
        // デバイス障害等による終了時の最終後片付け。通常の切替ではこの経路を使わない。
        retiring_ = true;
        if (current_) current_->OnExit(context_);
        current_.reset();
        retiring_ = false;
    }
    status_ = SceneStatus::Stopped;
    busy_ = false;
}

// 通常時はSceneへ任せる。解放待ちや失敗中はManager自身が状態を見せる。
ScenePresentation SceneManager::GetPresentation() const
{
    if (status_ == SceneStatus::Ready && current_) return current_->GetPresentation();
    if (status_ == SceneStatus::WaitingForRelease)
    {
        const auto snapshot = context_.memory.GetArena(ArenaType::Scene)->GetSnapshot();
        return { { 0.03f, 0.03f, 0.05f, 1 },
            L"Loading | waiting blocks: " + std::to_wstring(snapshot.blocks.activeAllocations) +
            L" / operations: " + std::to_wstring(snapshot.inFlightOperations) + L" | Esc: Quit" };
    }
    if (status_ == SceneStatus::Failed)
        return { { 0.3f, 0.02f, 0.02f, 1 }, L"Scene error (Visual Studio Output) | Esc: Quit" };
    return { {}, L"No active scene" };
}
