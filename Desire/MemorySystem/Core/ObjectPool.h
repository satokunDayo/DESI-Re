#pragma once
// 同じ型を何度も作るための席予約。テンプレートなので実装もこのヘッダに置く。
#include "MemorySystem/Core/MemoryArena.h"
#include <cassert>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

template<class T>
class ObjectPool final
{
    // 配列・const・破棄時に例外を投げる型は対象外。返却処理でさらに例外を起こしたくない。
    static_assert(std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>, "Pool destructors must not throw.");
    static constexpr std::size_t InvalidIndex = (std::numeric_limits<std::size_t>::max)();

    // 実体は共有のStateに置く。ObjectPoolの変数が先に消えても、貸出Handleが返却先を守る。
    struct State final : PoolControl
    {
        explicit State(std::shared_ptr<MemoryArena> owner, std::size_t count)
            : arena(std::move(owner))
        {
            if (!arena || count == 0 || count > BlockAllocator::MaxAllocationSize / sizeof(T))
                throw std::invalid_argument("Invalid pool arena or capacity.");
            // Tをまとめて置く土台。alignof(T)を渡すのでalignas(64)等もそのまま通る。
            slab = arena->Allocate(count * sizeof(T), alignof(T));
            if (!slab) throw std::bad_alloc();
            // 返却中にvectorの再確保を起こさないよう、空き席リストの容量も先に取る。
            freeIndices.reserve(count);
            for (std::size_t i = count; i != 0; --i) freeIndices.push_back(i - 1);
        }
        ~State() override { assert(active == 0); }

        // まず空き席だけ確保。activeにはコンストラクタ実行中の席も含める。
        std::size_t Reserve()
        {
            std::lock_guard lock(mutex);
            if (closing || !arena->IsOpen() || freeIndices.empty()) return InvalidIndex;
            const auto index = freeIndices.back();
            freeIndices.pop_back();
            ++active; // Includes an object whose constructor is still running.
            return index;
        }
        // 土台の先頭 + 席番号×sizeof(T)。アドレスを計算するだけで、Tはまだ誕生していない。
        T* Address(std::size_t index) const noexcept
        {
            return reinterpret_cast<T*>(static_cast<std::byte*>(slab.Data()) + index * sizeof(T));
        }
        // 空き席を返して貸出数を減らす。構築失敗時にも通常の返却時にも使う。
        void Cancel(std::size_t index) noexcept
        {
            std::lock_guard lock(mutex);
            assert(active != 0);
            freeIndices.push_back(index); // Capacity was reserved at construction.
            --active;
        }
        // Tを破棄してから席を返す。ユーザーのデストラクタはプールのロック外で呼ぶ。
        void Return(std::size_t index, T* object) noexcept
        {
            // User destructors execute without holding the pool lock.
            std::destroy_at(object);
            Cancel(index);
        }
        // 一度閉店したプールは再開しない。貸出ゼロになるまで土台の解放だけ待つ。
        bool TryClear() override
        {
            MemoryBlock returned;
            {
                std::lock_guard lock(mutex);
                closing = true;
                if (active != 0) return false;
                returned = std::move(slab);
                freeIndices.clear();
            }
            // ロックを抜けてからArena
            // へ土台を返す。別の管理者を呼ぶときはロック順に注意。
            returned.Reset();
            return true;
        }
        // 貸出数のスナップショット。Tの中身を同時に触ってよいという保証ではない。
        std::size_t GetActiveCount() const
        {
            std::lock_guard lock(mutex);
            return active;
        }

        std::shared_ptr<MemoryArena> arena;
        MemoryBlock slab;
        mutable std::mutex mutex;
        std::vector<std::size_t> freeIndices;
        std::size_t active = 0;
        bool closing = false;
    };

public:
    // 返却忘れ対策の持ち手。Getで借りたポインタをdeleteせず、このHandleをResetする。
    class Handle final
    {
    public:
        Handle() = default;
        ~Handle() { Reset(); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        // move元を空にして、唯一の返却担当を移す。Handleはコピーできない。
        Handle(Handle&& other) noexcept
            : state_(std::move(other.state_)), object_(std::exchange(other.object_, nullptr)),
              index_(std::exchange(other.index_, InvalidIndex)) {}
        // 元から持っていたTを返してから、新しいTの担当になる。
        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                state_ = std::move(other.state_);
                object_ = std::exchange(other.object_, nullptr);
                index_ = std::exchange(other.index_, InvalidIndex);
            }
            return *this;
        }
        // Stateを一時所有したままTを返す。返却完了までは土台が消えない。
        void Reset() noexcept
        {
            auto owner = std::move(state_);
            T* object = std::exchange(object_, nullptr);
            const auto index = std::exchange(index_, InvalidIndex);
            if (owner) owner->Return(index, object);
        }
        // ポインタや参照は借り物。空のHandleへの->や*、Reset後の利用は呼ぶ側で避ける。
        T* Get() const noexcept { return object_; }
        T* operator->() const noexcept { return object_; }
        T& operator*() const noexcept { return *object_; }
        explicit operator bool() const noexcept { return object_ != nullptr; }

    private:
        friend class ObjectPool;
        Handle(std::shared_ptr<State> owner, T* object, std::size_t index)
            : state_(std::move(owner)), object_(object), index_(index) {}
        // この宣言はHandle側の所有。Pool本体も同じStateを共有し、最後の所有がなくなるまで残る。
        std::shared_ptr<State> state_;
        T* object_ = nullptr;
        std::size_t index_ = InvalidIndex;
    };

    // 土台を確保してArenaの目録へ登録。途中でArenaが閉じたら失敗して自動で片付く。
    ObjectPool(std::shared_ptr<MemoryArena> arena, std::size_t capacity)
        : state_(std::make_shared<State>(std::move(arena), capacity))
    {
        if (!state_->arena->RegisterPool(state_))
            throw std::runtime_error("The arena closed while creating a pool.");
    }
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

    template<class... Args>
    // 引数をTのコンストラクタへそのまま渡す。満席・閉店・move済みなら空のHandle。
    [[nodiscard]] Handle Rent(Args&&... args)
    {
        auto owner = state_;
        if (!owner) return {};
        const auto index = owner->Reserve();
        if (index == InvalidIndex) return {};
        try
        {
            // 確保済みの住所にTを構築する。newで別の住所を取っているわけではない。
            T* object = std::construct_at(owner->Address(index), std::forward<Args>(args)...);
            return Handle(std::move(owner), object, index);
        }
        // Tの構築が失敗しても席は必ず戻す。中途半端なTにはデストラクタを呼ばない。
        catch (...) { owner->Cancel(index); throw; }
    }
    std::size_t GetActiveCount() const { return state_ ? state_->GetActiveCount() : 0; }
    bool TryClear() { return !state_ || state_->TryClear(); }

private:
    std::shared_ptr<State> state_;
};
