#pragma once
// Win32の窓を1つ所有するクラス。絵はRenderer、入力の状態はInputが担当する。
#include <Windows.h> // 多分俺(2002年製造)より年上

// エンジンに呼ばれるヘッダ
class Window final
{
public:
    // Windows OSから与えられるHINSTANCEを握りしめ顕現
    explicit Window(HINSTANCE instance);
    // 窓とクラス登録を片付ける。EngineではGPU側を先に終わらせてからここへ来る。
    ~Window();

    // 同じウィンドウを2つに増殖させようとする愚か者をコンパイルエラーで処す
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 幅、高さ、どう見せるかを指定。
    bool Create(int clientWidth, int clientHeight, int showCommand);

    // OSから飛んでくる「マウス動いたよ！」「ボタン押されたよ！」
    // という無限の手紙（メッセージ）をひたすら処理し続ける
    bool ProcessMessages();

    // レンダラへ窓のハンドルを貸す。所有権はWindowに残るので、借りた側でDestroyWindowしない。
    HWND GetHandle() const { return handle_; }

private:
    // OSからの全ての手紙が怒涛の勢いで叩きつけられる魔の関数領域
    // staticにしないとC++のクラスとWin32のC言語APIが噛み合わない

    static LRESULT CALLBACK WindowProc(
        HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

    // Windows OSに登録する名前（SatoEngineWindow）。
    // これはC++の型名ではなく、Win32へ登録する窓の種類の名前。
    static constexpr wchar_t ClassName[] = L"SatoEngineWindow";

    // このアプリのモジュールを指すハンドル。管理者権限などの許可証ではない。
    HINSTANCE instance_; // このアプリのモジュール識別ハンドル
    // 窓を指定するためのハンドル。自前のC++オブジェクトとしてdeleteするものではない。
    HWND handle_ = nullptr; // 顕現した窓を指定するハンドル

    // ATOM（アトム）
    // ウィンドウクラス（C++のクラスではなくOS側のクラス）を登録した時に貰える識別番号。
    ATOM classAtom_ = 0;
};