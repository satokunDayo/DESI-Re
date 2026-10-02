// ここが起動の入口。エンジンを組み立て、シーンの名簿を渡して、あとはRunに任せる。
#include "Engine.h"
#include "StarterScenes.h"
#include <exception>

// Windowsから最初に呼ばれる。instanceはこのアプリの識別用ハンドル。
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    try
    {
        // このスコープの住人。正常終了でも例外でも、最後はデストラクタが片付ける。
        Engine engine(instance);
        // 登録するのはシーンの「作り方」。この時点でステージやGPU画像はまだ作らない。
        RegisterStarterScenes(engine.GetScenes());

        //ハジをかくなよ
        // 窓とDX12を準備する。初期化前にRunへ突撃すると何も描けない。順番大事。
        if (!engine.Initialize(showCommand))
        {
            MessageBoxW(
                nullptr,
                L"Engine initialization failed.",
                L"DESI:Re",
                MB_OK | MB_ICONERROR);

            return 1;
        }
        //エンジン点火！！ぶおおおおん！！
        return engine.Run();
    }
    // 起動失敗を無言で終わらせないための最後の受け皿。理由を出して終了する。
    catch (const std::exception& error)
    {
        MessageBoxA(
            nullptr,
            error.what(),
            "DESI:Re",
            MB_OK | MB_ICONERROR);

        return 1;
    }
}