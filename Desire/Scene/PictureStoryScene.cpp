#include "Scene/PictureStoryScene.h"
#include "Resource/ResourceManager.h"
#include "Resource/TextureLoader.h"
#include "Scene/SceneManager.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <utility>

// コンストラクタ（初期化）
// ここでは設定を受け取るだけ。画像はまだ読み込まない。
// 引数: 
// - folder: 画像が入っているAssetsフォルダからの相対パス
// - nextScene: 紙芝居が終わった後に遷移するシーンの名前
// - allowSkip: スキップ機能（F2キーなど）を許可するかどうか

PictureStoryScene::PictureStoryScene(
    std::filesystem::path folder,
    std::string nextScene,
    bool allowSkip)
    : folder_(std::move(folder)),
    nextScene_(std::move(nextScene)),
    allowSkip_(allowSkip)
{
    // セキュリティ＆エラー防止のためのパス検証。
    // フォルダ指定が空っぽ、あるいは「C:\」のような絶対パスの場合はエラーにしてプログラムを止める。
    // (Assetsフォルダを基準にするルールを厳格に守らせるため)
    if (folder_.empty() || folder_.has_root_path())
    {
        throw std::invalid_argument(
            "Story folder must be relative to Assets.");
    }

    // パスの中に..（1つ上の階層へ戻る）が含まれていないかチェック。
    // これを許すと、意図しないシステムファイルなどを読み込もうとするバグに繋がるため。
    for (const auto& part : folder_)
    {
        if (part == "..")
        {
            throw std::invalid_argument(
                "Story folder cannot contain '..'.");
        }
    }
}

// シーン開始時の処理 (OnEnter)
// シーンに入ったら、画像の一覧を作り、最初の1枚を開く。
void PictureStoryScene::OnEnter(SceneContext& context)
{
    // 画像を読み込むための機能(TextureLoader)が存在するかチェック。
    if (!context.textures)
    {
        throw std::runtime_error(
            "PictureStoryScene needs TextureLoader.");
    }

    // 以前の状態が残らないよう、念のため初期化。
    page_.reset();    // 現在表示中の画像を捨てる
    pages_.clear();   // 画像パスのリストを空にする
    pageIndex_ = 0;   // ページ番号を0（最初）に戻す

    // Assetsフォルダのルートパスと、指定されたフォルダ名を合体させてフルパスを作る。
    const auto directory =
        context.resources.GetRootPath() / folder_;

    // そのフォルダが本当に存在するかチェック。なければエラーを投げる。
    if (!std::filesystem::is_directory(directory))
    {
        throw std::runtime_error(
            "Story folder not found: " + directory.string());
    }

    // フォルダの中身を1つずつチェックして画像を探す。
    // （このフォルダの直下だけを調べる。さらに下のフォルダには潜らない）
    for (const auto& entry :
        std::filesystem::directory_iterator(directory))
    {
        // フォルダなどは無視し、普通のファイルだけを対象にする。
        if (!entry.is_regular_file())
            continue;

        // 拡張子（.png など）を取得する。
        auto extension = entry.path().extension().wstring();

        // 拡張子をすべて小文字に変換する処理。
        // （ユーザーが「.PNG」や「.Png」といった名前で保存していても、正しく認識できるようにするため）
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](wchar_t c)
            {
                return static_cast<wchar_t>(std::towlower(c));
            });

        // 拡張子が .png だったら、表示するリスト(pages_)に加える。
        if (extension == L".png")
        {
            // TextureLoaderには「Assetsからの相対パス」を渡す必要があるため、
            // フォルダ名とファイル名をくっつけてリストに保存する。
            pages_.push_back(folder_ / entry.path().filename());
        }
    }

    // フォルダからファイルを取得した段階では順番がバラバラ（OSの仕様）なので、
    // アルファベット順（001.png → 002.png → 003.png）に綺麗に並べ替える。
    std::sort(pages_.begin(), pages_.end());

    // もしフォルダの中に1枚もPNG画像がなかったらエラーにして止める。
    if (pages_.empty())
    {
        throw std::runtime_error(
            "No PNG pages in story folder: " + directory.string());
    }

    // 準備が整ったので、1ページ目（index:0）を表示する。
    ShowPage(context, 0);
}

//指定したページを開く処理 (ShowPage)
void PictureStoryScene::ShowPage(
    SceneContext& context,
    std::size_t index)
{
    // context.textures->Load で新しい画像を読み込む。
    // 「新画像を読み込めてから、page_ 変数に上書き(差し替え)する」という順番になるため、
    // この一瞬だけ旧画像と新画像がメモリ上に共存する。
    // これにより、読み込み中に画面が一瞬真っ黒になるのを防いでいる。
    page_ = context.textures->Load(pages_.at(index));
    pageIndex_ = index; // 現在のページ番号を更新
}
// シーン終了処理 (Finish)
// 最後まで読んだときと、スキップしたときの共通処理。
void PictureStoryScene::Finish(SceneContext& context)
{
    if (nextScene_.empty())
    {
        // 次のシーンが設定されていなければ、ゲーム（またはシステム）を終了するリクエストを出す。
        context.scenes.RequestQuit();
    }
    else
    {
        // 次のシーンが設定されていれば、そのシーンへの切り替えリクエストを出す。
        context.scenes.RequestChange(nextScene_);
    }
}

// 毎フレームの更新処理（入力判定など） (OnUpdate)
// 入力を見て、ページをめくる。
void PictureStoryScene::OnUpdate(
    SceneContext& context,
    const SceneInput& input,
    double)
    // 最後の引数(deltaTimeなど)は今回は使わないので変数名を省略している
{
    // キャンセルボタン（Escキーなど）が押されたら、強制終了する。
    if (input.cancel)
    {
        context.scenes.RequestQuit();
        return; // これ以上処理しない
    }

    // スキップが許可されていて、かつ代替ボタン（F2キーなど）が押されたら、シーンを終わらせて次へ進む。
    if (allowSkip_ && input.alternate)
    {
        Finish(context);
        return;
    }

    // 決定ボタン（EnterやSpace、マウスクリックなど）が押されていなければ、何もしないで終わる。
    if (!input.confirm)
        return;

    // --- ここから下は「決定ボタンが押された」ときの処理 ---

    // 次のページがまだあるか確認する。
    if (pageIndex_ + 1 < pages_.size())
    {
        // まだあるなら、次のページを表示する。
        ShowPage(context, pageIndex_ + 1);
    }
    else
    {
        // もう最後のページだったら、シーンを終了して次へ進む。
        Finish(context);
    }
}
// シーンから抜けるときの処理 (OnExit)
// シーン退出時、自分が持っていた画像と一覧を手放す。
// (メモリリークを防ぐため、持っているデータを空っぽにする)
void PictureStoryScene::OnExit(SceneContext&) noexcept
{
    page_.reset();    // 画像メモリを解放
    pages_.clear();   // リストをクリア
    pageIndex_ = 0;
}

// 描画データの作成処理 (GetPresentation)
// 今表示している画像を、Renderer（描画システム）への描画依頼としてまとめる。
ScenePresentation PictureStoryScene::GetPresentation() const
{
    ScenePresentation view;
    // 背景のクリアカラー（余白の色）を真っ黒 (Red:0, Green:0, Blue:0, Alpha:1) に設定。
    view.clearColor = { 0, 0, 0, 1 };

    // captionはウィンドウ上部のタイトルバーなどに表示される文字。
    // 「紙芝居 1 / 5 | Enter / Space：次へ | Esc：終了」のような文字列を動的に組み立てる。
    view.caption =
        L"紙芝居 " + std::to_wstring(pageIndex_ + 1)
        + L" / " + std::to_wstring(pages_.size())
        + L" | Enter / Space：次へ | Esc：終了";

    // スキップ可能なら、ウィンドウのタイトルにも操作方法を追記しておく。
    if (allowSkip_)
        view.caption += L" | F2：スキップ";

    // もし画像がまだ読み込めていなければ、黒画面だけを返す（エラーで落ちないための安全対策）。
    if (!page_)
        return view;

    // --- 画像の表示サイズと位置の計算 ---

    // 今のRendererのSprite座標は1280×720。
    // （実際のウィンドウサイズや、レトロ風にするための内部解像度320×180とは別の「配置を計算するためのキャンバスサイズ」）

    constexpr float canvasWidth = 1280.0f;
    constexpr float canvasHeight = 720.0f;

    // 読み込んだ画像の実際のピクセル幅と高さを取得。
    const float width = static_cast<float>(page_->Width());
    const float height = static_cast<float>(page_->Height());

    // 【アスペクト比（縦横比）を維持したまま、画面に収めるための計算】
    // 画面の幅÷画像の幅、画面の高さ÷画像の高さを計算し、
    // 「小さい方」の倍率を採用することで、画面からはみ出さずに最大化できる。
    const float scale = (std::min)(
        canvasWidth / width,
        canvasHeight / height);

    // 描画用の構造体にデータを詰める。
    SpriteDraw image;
    image.texture = page_; // 描画する画像データ
    // 先ほど計算した倍率を掛けて、最終的な描画サイズを決定。
    image.width = width * scale;
    image.height = height * scale;

    // 【画像を画面のど真ん中に配置するための計算】
    // (画面の幅 - 描画する幅) を計算すると「余白の合計」が出る。
    // それを 0.5倍(半分) にすることで、左と右（上と下）に均等に余白を振り分け、中央揃えになる。
    image.x = (canvasWidth - image.width) * 0.5f;
    image.y = (canvasHeight - image.height) * 0.5f;

    // 計算した画像データを描画リストに追加。
    // （コメントにある通り、ここから先はシステム側がCRTフィルター等を通して画面に出力する）
    view.sprites.push_back(image);

    // 描画依頼をシステムへ返す。
    return view;
}