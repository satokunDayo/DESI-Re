// Window.hで約束した窓の作成と終了を実行する場所。Win32の後始末もここで引き受ける。
#include "Window.h"
//エンジンに呼ばれるWindows.hの中身。いやん、見ないで。

Window::Window(HINSTANCE instance)
    : instance_(instance)
{

}

Window::~Window()
{
    // 終末処理
    if (handle_ != nullptr)
    {
        // 顕現させたウィンドウを粉砕
        DestroyWindow(handle_);
    }

    if (classAtom_ != 0)
    {
        // OSの戸籍から自分たちの痕跡を抹消する。
        // これを忘れると、次に起動した時に「もうその名前使われてるよ」と言われる。電話番号かよ
        UnregisterClassW(ClassName, instance_);
    }
}

// 成功したらtrue。クラス登録と窓作成の途中で失敗しても、残った分はデストラクタで片付ける。
bool Window::Create(int clientWidth, int clientHeight, int showCommand)
{
    // 既にウィンドウをがあるか、サイズが虚無なら門前払い。
    if (classAtom_ != 0 || clientWidth <= 0 || clientHeight <= 0)
    {
        return false;
    }

    // Windows OSというお役所に提出するWNDCLASSEXWの作成。
    WNDCLASSEXW info{};
    info.cbSize = sizeof(info);               // 履歴書の紙のサイズ（儀式）
    // OSに「通知の送り先はこの関数」と教える。普通の非staticメンバー関数はそのまま渡せない。
    info.lpfnWndProc = &Window::WindowProc;   // 苦情処理の担当者窓口（コールバック）
    info.hInstance = instance_;               // 許可証
    info.hCursor = LoadCursorW(nullptr, IDC_ARROW); // カーソルの形
    info.hbrBackground = GetSysColorBrush(COLOR_WINDOW); // 背景色
    info.lpszClassName = ClassName;           // 俺たちの名

    //受理されると魂の番号（ATOM）がもらえる。
    classAtom_ = RegisterClassExW(&info);
    if (classAtom_ == 0)
    {
        return false; // 受理不可、我悲壮感壮大
    }

    // 1280x720の窓を頼むと、OSはタイトルバーと枠を含めて1280x720にする
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rect{ 0, 0, clientWidth, clientHeight };

    //1280x720にしたいんで、枠の分を逆算してデカくして！
    if (!AdjustWindowRectEx(&rect, style, FALSE, 0))
    {
        return false;
    }

    //指定したサイズの窓を 錬成
    handle_ = CreateWindowExW(
        0, ClassName, L"DESI:Re", style,
        CW_USEDEFAULT, CW_USEDEFAULT,           // 出現位置はOSにお任せ
        rect.right - rect.left, rect.bottom - rect.top, // さっき逆算したサイズ
        nullptr, nullptr, instance_, nullptr);

    if (handle_ == nullptr)
    {
        return false; // 錬成失敗。右腕持ってかれた？
    }
    
    //さとくんエンジンだよ
    ShowWindow(handle_, showCommand);
    return true;
}

// 通知がないときに待ち続けない方式。空になったらゲームループへ戻る。
bool Window::ProcessMessages()
{
    MSG message{};

    // 手紙ある？無いならいいや、仕事（描画）に戻るわ！」ができる PeekMessageを使う
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        // もう終わりの時間だよ（WM_QUIT）
        if (message.message == WM_QUIT)
        {
            // 世界の終わりをEngineに伝える（メインループ脱出）
            return false;
        }

        TranslateMessage(&message); // キーボード入力をいい感じに翻訳
        DispatchMessageW(&message); // 担当者（WindowProc）に手紙をぶん投げる
    }
    return true;
}

// 苦情処理窓口（ウィンドウプロシージャ）
// CALLBACKは呼び出し規約、LRESULTは返事の型。thisは渡ってこない関数。
LRESULT CALLBACK Window::WindowProc(
    HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    // バツ（×）ボタン、押されちゃったね....
    // ここでは窓を即破壊せず、終了したいとだけ伝える。GPUが使う窓を先に消さないため。
    if (message == WM_CLOSE)
    {
        // 終了通知（WM_QUIT）を自分の手紙箱に投函する。
        // これを巡り巡ってさっきの PeekMessage が拾い上げ、世界が終わる。
        PostQuitMessage(0);
        return 0; // 処理しました！というサイン
    }

    // 自分で扱わない通知（数千種類ある）の山だ！！
    // Windowsの標準処理（DefWindowProc）に丸投げだ！
    return DefWindowProcW(handle, message, wParam, lParam);
}