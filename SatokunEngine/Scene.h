#pragma once
// Sceneの共通の約束と、Engineとの受け渡し用データ。TitleでもGameでも同じ入口を使う。
#include <array>
#include <string>
#include <vector>
#include "SpriteDraw.h"
class TextureLoader;
class MeshLoader;
class World;
class Camera;

class MemoryManager;
class ResourceManager;
class SceneManager;

// サービスの借用窓口。この中にMemoryManager等のコピーを作るわけではない。
struct SceneContext
{
    // CPU領域を借りる先。実際の所有者はEngineなのでSceneより長生きする。
    MemoryManager& memory;
    // ファイルの元バイト列を読みたいときの窓口。PNGの画素化はTextureLoaderへ。
    ResourceManager& resources;
    // 次のSceneへ行きたいときはここへ予約。自分自身をその場でdeleteしない。
    SceneManager& scenes;
    TextureLoader* textures = nullptr; // nullptrなら描画を伴わないロジックテスト
    MeshLoader* meshes = nullptr;
};

// キー番号をシーンへ直渡しせず、操作の意味を渡す。
struct SceneInput
{
    bool confirm = false;
    bool cancel = false;
    bool pause = false;
    bool event = false;
    bool alternate = false;
    // 移動は押しっぱなし、視点は今フレームの差分、カメラ切替は押した瞬間。
    float moveX=0,moveY=0,lookX=0,lookY=0,turnX=0,turnY=0;
    bool switchCamera=false;
};

// 今フレームにどう見せるかの返事。ゲーム進行の本体をここへ移すものではない。
struct ScenePresentation
{
    std::array<float, 4> clearColor{ 0.08f, 0.18f, 0.32f, 1.0f };
    std::wstring caption;
    // 画面上の画像依頼。画像ハンドルは共有するが、Worldの物とは別の描画パス。
    std::vector<SpriteDraw> sprites;
    // Sceneが持っているWorldを借りるだけ。Scene退出後まで保存すると宛先が消える。
    const World* world = nullptr;
    const Camera* camera = nullptr; // Sceneが所有。GetPresentation後、退出前に描画する。
    // CRTの後に描く画像。窓の実ピクセル座標で、デバッグ表示やプレビューUI向け。
    std::vector<SpriteDraw> overlaySprites;
};

// 各Sceneが継承する土台。virtual経由で、実際のTitleやGameの処理へ呼び分ける。
class Scene
{
public:
    Scene() = default;
    virtual ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // 入場時に必要な画像やWorldをそろえる。途中失敗でもOnExitで片付けられるようにする。
    virtual void OnEnter(SceneContext&) {}
    // 1描画フレームにつき1回の入力・イベント処理。引数の秒数は固定とは限らない。
    virtual void OnUpdate(SceneContext&, const SceneInput&, double) {}
    // 1/60秒刻みのゲーム更新。移動や物理をここへ接続していく。
    virtual void OnFixedUpdate(SceneContext&, double) {}
    // OnEnterが途中で失敗した場合も呼ばれる。途中までの確保も片付ける。
    virtual void OnExit(SceneContext&) noexcept {}
    // 描く内容を返す。ここでゲーム時間を進めると、呼ばれた回数で速度が変わるのでやらない。
    virtual ScenePresentation GetPresentation() const = 0;
};
