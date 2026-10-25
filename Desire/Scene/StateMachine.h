#pragma once
// 今は何の振る舞いをするかを切り替える小さな司令塔。Game用と敵AI用は別インスタンスで使う。
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

template<class Context>
// 状態の共通インターフェース。Contextの型を決めるので、別のゲームデータと混ざりにくい。
class IState
{
public:
    virtual ~IState() = default;
    // 入ったときに1回。再入場で使うカウンターなどは各State側で戻しておく。
    virtual void Enter(Context&) {}
    // 毎フレームの判断。Enter/Exitを自分で直接連鎖させず、Managerへ切替を予約する。
    virtual void Update(Context&, double) {}
    // 固定時間の動作。止めたいStateでは未実装のままにすれば何もしない。
    virtual void FixedUpdate(Context&, double) {}
    // 出るときの片付け。例外を投げない約束なので、失敗し得る処理を安易に置かない。
    virtual void Exit(Context&) noexcept {}
};

// Contextの寿命はStateMachineより長くする。操作はメインスレッド専用。
template<class Context>
class StateMachine final
{
public:
    // Contextを借りる。StateMachineのほうだけ長生きさせると、借り先が消える。
    explicit StateMachine(Context& context) : context_(context) {}
    ~StateMachine() { Stop(); }
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;

    template<class State, class... Args>
    // 状態は登録時に生成して使い回す。SceneのFactory方式とはここが違う。
    void Register(std::string id, Args&&... args)
    {
        static_assert(std::is_base_of_v<IState<Context>, State>);
        if (busy_) throw std::logic_error("Cannot register a state during callbacks.");
        if (id.empty() || states_.contains(id))
            throw std::invalid_argument("Empty or duplicate state ID: " + id);
        states_.emplace(std::move(id), std::make_unique<State>(std::forward<Args>(args)...));
    }

    // 今使っているStateや切替予定のStateは、その場で名簿から抜けない。
    bool Remove(std::string_view id)
    {
        if (busy_ || id == currentId_ || (pending_ && id == *pending_)) return false;
        const auto found = states_.find(id);
        if (found == states_.end()) return false;
        states_.erase(found);
        return true;
    }

    // 同じ確定区間では先着1件。実際のEnter/ExitはUpdate末尾で実行する。
    // 先着1件を予約するだけ。Updateの途中で呼んでも自分のStateを即破棄しない。
    bool RequestChange(std::string_view id)
    {
        if (exiting_) return false;
        if (!states_.contains(id)) throw std::invalid_argument("Unknown state: " + std::string(id));
        if (pending_) return false;
        pending_ = std::string(id);
        return true;
    }

    // 予約がなければ現在のUpdate、その後に予約分のExit→Enter。最初から予約済みなら旧Updateは飛ばす。
    void Update(double seconds)
    {
        ValidateDelta(seconds);
        Guard guard(busy_);
        if (current_ && !pending_) current_->Update(context_, seconds);
        if (!pending_) return;
        // 今回確定する予約だけ取り出す。新しいEnterから出た次の予約は次回まで持ち越す。
        auto target = std::move(*pending_);
        pending_.reset();
        LeaveCurrent();
        currentId_ = std::move(target);
        current_ = states_.at(currentId_).get();
        try { current_->Enter(context_); }
        catch (...)
        {
            LeaveCurrent();
            pending_.reset();
            throw;
        }
        // Enter中のRequestChangeはここで再処理せず、次のUpdateへ残す。
    }

    // 切替予約が出ている状態を追加で動かさない。次の通常更新で切替を確定する。
    void FixedUpdate(double seconds)
    {
        ValidateDelta(seconds);
        Guard guard(busy_);
        if (current_ && !pending_) current_->FixedUpdate(context_, seconds);
    }

    // 予約を捨てて現在のExitを呼ぶ。登録済みStateのmapまでは消さない。
    bool Stop() noexcept
    {
        if (busy_) return false;
        busy_ = true;
        pending_.reset();
        LeaveCurrent();
        busy_ = false;
        return true;
    }

    const std::string& GetCurrentId() const noexcept { return currentId_; }
    bool HasPendingChange() const noexcept { return pending_.has_value(); }

private:
    // 再帰Updateを検出する見張り。例外のときもデストラクタでbusyを戻す。
    struct Guard
    {
        explicit Guard(bool& flag) : flag_(flag)
        {
            if (flag_) throw std::logic_error("Recursive StateMachine update.");
            flag_ = true;
        }
        ~Guard() { flag_ = false; }
        bool& flag_;
    };
    static void ValidateDelta(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0.0)
            throw std::invalid_argument("Invalid state delta time.");
    }
    // Exit中の切替予約は受け付けない。出ていく人に次の受付を任せない。
    void LeaveCurrent() noexcept
    {
        exiting_ = true;
        if (current_) current_->Exit(context_);
        current_ = nullptr;
        currentId_.clear();
        exiting_ = false;
    }

    // Stateたちが操作する対象。所有していないので寿命は呼び出し側と合わせる。
    Context& context_;
    // Stateの実体はこのmapが所有。current_はその中の1個を指しているだけ。
    std::map<std::string, std::unique_ptr<IState<Context>>, std::less<>> states_;
    IState<Context>* current_ = nullptr;
    std::string currentId_;
    // 空なら予約なし、文字列が入っていれば次に移る状態名。
    std::optional<std::string> pending_;
    bool busy_ = false;
    bool exiting_ = false;
};
