// GetAsyncKeyStateをここに隔離。ゲーム側がキーを何度も直接ポーリングしなくて済む。
#include "EngineSelf/Input.h"

void Input::Poll(HWND window)
{
    const bool wasActive = active_;
    // 最前面にいて、最小化されていない自分の窓だけ入力を受け付ける。
    active_ = window != nullptr && GetForegroundWindow() == window && !IsIconic(window);
    // 押した瞬間の情報は毎フレーム使い切り。前回分を残すと永久連打になる。
    pressed_.fill(false);
    mouseDelta_ = {};

    // フォーカスを失ったら押しっぱなし情報も捨てる。復帰時の差分も取り直す。
    if (!active_)
    {
        held_.fill(false);
        mouseReady_ = false;
        return;
    }

    for (int key = 0; key < 256; ++key)
    {
        // 最上位ビットだけを見る。押した瞬間は前フレームとの比較で作る。
        const bool now = (GetAsyncKeyState(key) & 0x8000) != 0;
        // 今ON、前はOFF、かつ前フレームも操作中。この3条件で押した瞬間を作る。
        pressed_[key] = wasActive && now && !held_[key];
        held_[key] = now;
    }

    // 初版は右ドラッグ。カーソルの固定やRaw Inputはまだ使わない。
    POINT cursor{};
    // 右ボタンを押していない間はマウス移動をゲーム操作として数えない初版仕様。
    if (GetCursorPos(&cursor))
    {
        // ドラッグ開始直後は比較用の前位置がないので、差分を出さず基準位置だけ保存する。
        if (mouseReady_ && wasActive)
        {
            mouseDelta_.x = cursor.x - previousMouse_.x;
            mouseDelta_.y = cursor.y - previousMouse_.y;
        }
        previousMouse_ = cursor;
        mouseReady_ = true;
    }
    else
    {
        mouseReady_ = false;
    }
}

// 呼び出し側が変なキー番号を渡しても、配列の外へ読みに行かない。
bool Input::GetKey(int key) const noexcept
{
    return key >= 0 && key < 256 && held_[key];
}

// 固定更新で何度もこの値を使うと多重消費になる。今はSceneの通常更新へ1回渡す。
bool Input::GetKeyDown(int key) const noexcept
{
    return key >= 0 && key < 256 && pressed_[key];
}
