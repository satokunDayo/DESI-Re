// ここが起動の入口。エンジンを組み立て、シーンの名簿を渡して、あとはRunに任せる。
#include <filesystem>
#include "EngineSelf/Engine.h"
#include "Game/StarterScenes.h"
#include <exception>
#include <string>
#include "Scene/PictureStoryScene.h"


#include "Game/Stage1Scene.h"

// Windowsから最初に呼ばれる。instanceはこのアプリの識別用ハンドル。
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    try
    {
        // このスコープの住人。正常終了でも例外でも、最後はデストラクタが片付ける。
        Engine engine(instance);
        // 登録するのはシーンの「作り方」。この時点でステージやGPU画像はまだ作らない。
        // 
		//ロゴの表示切替
        // 
		// 表示してくれたらうれしい。ロゴね、こだわったんだ。 
        // けどもし表示したくなければ、falseにしてもいいんだよ。うん。
		// いやほんと、ロゴの有無はゲームの内容に関係ないから、気にしないでいいんだよ。うん。
		// 最大2.5秒の表示（Enterでスキップ可）だけどさ、迷惑だって思うなら別にいいんだよ。うん。
		// いやほんっとに、気にしないでいいんだよ。うん。うん。
        // 
        // 下のshowBootLogoで切り替える。trueならロゴあり、falseなら本編へ直行。
        // firstSceneが空なら、ロゴありは表示後終了、ロゴなしはそのまま終了。
        

        // ゲームのSceneを作ったら、ここで登録する。StarterScenesはロゴ専用のまま。
        // 例: scenes.Register<TitleScene>("Title"); // TitleSceneを作成・includeしてから使う。

        auto& scenes = engine.GetScenes();


        // Stage1の設定。
        MazeStageSettings stage1;
        stage1.width = 17;
        stage1.height = 17;
        stage1.extraHoles = 3;
        stage1.saveFile = "Maps/generated-stage01.map";

        stage1.nextScene = "Ending";
        stage1.gameOverScene = "GameOver";
        scenes.Register<Stage1Scene>("Stage1", stage1);
        // 紙芝居が終わったらStage1へ。
        scenes.Register<PictureStoryScene>(
            "BeforeStage1",
            std::filesystem::path("Story/BeforeStage1"),
            std::string("Stage1"),
            true
        );

        scenes.Register<PictureStoryScene>(
            "GameOver",
            std::filesystem::path("Story/GameOver"),
            std::string{},  // 画像を読み終えたら終了。
            false
        );

        scenes.Register<PictureStoryScene>(
            "Ending",
            std::filesystem::path("Story/Ending"),
            std::string{},
            false
        );


        constexpr bool showBootLogo = true;
        const std::string firstScene = "Stage1";

        RegisterStarterScenes(scenes, showBootLogo, firstScene);

        if (scenes.IsQuitRequested()) return 0; // ロゴなし＋本編なしなら窓を作らず終了。



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
