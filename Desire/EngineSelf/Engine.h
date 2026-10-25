#pragma once
// エンジン本体。窓・時間・入力・Scene・描画・メモリの持ち主であり、呼ぶ順番の責任者。
// 一度呼ばれたら二度目は来んなというおもてなし

#include "EngineSelf/Window.h"
#include "EngineSelf/Timer.h"
#include "Render/D3D12Renderer.h"
// エンジンを構成する「三種の神器」（GUI、時間、描画）

#include "MemorySystem/Manager/MemoryManager.h"
#include "Resource/ResourceManager.h"
#include "Scene/SceneManager.h"
#include "EngineSelf/Input.h"
#include "Resource/TextureLoader.h"
#include "Resource/MeshLoader.h"
#include "Model/ModelLoader.h"

// final、つまりこいつがラストソルジャー、世界侵略: ロサンゼルス決戦思い出しちゃう。あれはすぐに小隊全員ついてきたけど。
class Engine final
{
    // CPU用の領域を最初に作り、最後まで生かす。後ろのサービスがこの領域を借りる。
    MemoryManager memory_; // System 64MiB + Scene 192MiB

    // Assetsの元ファイルを共有する係。画像のデコードやGPU転送はLoaderへ分担。
    ResourceManager resources_{ memory_, L"Assets" };

public:
    // 顕現（インスタンス化）の儀式。
    // explicit（明示的）指定により、コンパイラが勝手に気を利かせて
    // 別の型から暗黙の型変換で召喚してしまう事故を未然に防ぐ。C++の嗜み。
    explicit Engine(HINSTANCE instance);

    // ウィンドウやGPUを叩き起こす。失敗したら切腹
    bool Initialize(int showCommand);

    // メインループへの入り口。
    // ユーザーが「×」ボタンを押して世界を終わらせるまで終わらない
    int Run();
    // 登録窓口を借りて使ってもらう。SceneManagerそのものをコピーして渡すわけではない。
    SceneManager& GetScenes() noexcept { return scenes_; }

private:
    // 1回ぶんの論理時間は1/60秒。実時間の呼出間隔まで一定という意味ではない。
    void FixedUpdate(double deltaSeconds);

    //世界を描き出す儀式。
    // interpolationは固定更新の間を滑らかに描くための補間率。今は受け取るだけで、補間処理は未実装。
    void Render(double interpolation);

    // OSの窓の作成とメッセージ処理を担当。入力のポーリングはInput側。
    Window window_;

    // こいつが現実の経過時間を測ってくれないとフレームベースのゲームになっちゃう。

    Timer timer_;

    // 狂気（DirectX12の数千行のコード）をその身に宿すレンダラー。
    // おかげで俺たち楽できる。最高。愛してる。
    D3D12Renderer renderer_;
    // 画像ロードの配線。memory/resources/rendererが先にできているので参照を渡せる。
    TextureLoader textures_{ memory_, resources_, renderer_ };
    // 組み込みBox/Quadの共有窓口。FBXは次のModelLoaderへ。
    MeshLoader meshes_{ memory_, renderer_ };
    ModelLoader models_{ memory_, resources_, renderer_, textures_ };
    // renderer_より後に宣言し、renderer_が生きている間にSceneManagerを終了する。
    SceneManager scenes_{ memory_, resources_, [this] { renderer_.Flush(); }, &textures_, &meshes_, &models_ };
    // 押しっぱなしと押した瞬間を分けて記録する。フレームにつきPollは1回。
    Input input_;

    // 将来用に残っている時計。現時点では更新も参照もしていない。
    // 現在は未使用のメンバー。実際のサンプルのプレイ時間はGameScene側で数えている。
    double simulationSeconds_ = 0.0;

	//ゲームの内部解像度とCRTの有無
    static constexpr UINT InitialRenderWidth = 320;
    static constexpr UINT InitialRenderHeight = 180;
	// CRTの品質。NoneならCRT処理なし、Fastは簡易CRT、Fullは未実装
    static constexpr CrtQuality InitialCrtQuality = CrtQuality::Fast;

// -----------------------------------------------------------------------------
// Note: 本格的なCRTシェーダ（曲面歪み・色収差など）を採用しない理由
// 
// 画面をブラウン管のように丸く膨らませるには、ピクセルごとに sqrt(平方根)、
// pow(累乗)、sin/cos 等のGPUにとって計算コストが高い非線形な座標変換が必要になる。
// 当シェーダ(Fast版)は、それらを省き fmod や dot 等の最軽量の計算に留めている。
// 
// CRTエフェクトは「ゲームの内部解像度」ではなく「出力先のモニタ解像度」で実行される。
// フルHDなら約200万、4Kなら約800万ピクセルすべてに対し、大量のサンプリングと重い数学関数を
// 毎フレーム実行すると、それだけでGPUリソースを無駄に大きく消費してしまうため、
// パフォーマンスを最優先し、曲面歪みなどの処理は意図的に切り捨てている。
// -----------------------------------------------------------------------------
};