#pragma once
// 全オブジェクトで共通の表示設定。配置レイヤーと描画レイヤーを混同しないための型。

// 配置用のレイヤー・衝突グループとは別。独自名はconstexpr intで追加できる。
// 小さい番号から描く。名前付きの既定値であって、この4種類だけに制限してはいない。
namespace RenderLayers
{
    inline constexpr int Background = -100;
    inline constexpr int World = 0;
    inline constexpr int Foreground = 100;
    inline constexpr int UI = 200;
}
struct RenderSettings
{
    // 大分類の描画順。WorldとSpriteは別々の描画パスなので、両者をまたぐ総ソートではない。
    int layer = RenderLayers::World;
    // 同じ描画レイヤーの中の順番。3Dで深度有効なら、描画順だけで奥の物を手前には出せない。
    int orderInLayer = 0;
    // 描画を省くスイッチ。オブジェクト削除・AI停止・メモリ回収の指示ではない。
    bool visible = true;
};
