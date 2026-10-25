#pragma once
// 入力の記録帳。OSのキー状態を1回読み、Sceneへ渡すまで保持する。
#include <Windows.h>
#include <array>

class Input final
{
public:
    // 描画フレームごとに1回。固定更新の中からは呼ばない。
    void Poll(HWND window);
    // 押している間ずっとtrue。移動のように押し続ける操作向け。
    bool GetKey(int key) const noexcept;
    // 押したフレームだけtrue。決定ボタンを押しっぱなしで連打扱いにしない。
    bool GetKeyDown(int key) const noexcept;
    // 右ドラッグ中の前フレームとの差。SceneInputを通してFPSカメラへ渡せる。
    POINT GetMouseDelta() const noexcept { return mouseDelta_; }
    bool IsActive() const noexcept { return active_; }

private:
    // heldは押下中、pressedは今回押した瞬間。似てるけど別の情報。
    std::array<bool, 256> held_{};
    std::array<bool, 256> pressed_{};
    // 前のカーソル位置と、そこからの移動量を別々に持つ。
    POINT previousMouse_{};
    POINT mouseDelta_{};
    // 窓が操作対象かどうか。別アプリを触っている間にキャラが歩き出したら困る。
    bool active_ = false;
    bool mouseReady_ = false;
};
