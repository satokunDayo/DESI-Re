#pragma once
// 名前からオブジェクトのひな形を取り出す係。生まれた物の所有者はWorldのまま。
#include "GameObject.h"
#include <cstddef>
#include <map>
#include <string>
#include <string_view>

class World;

// メインスレッド専用。裏の読み込みスレッドからWorldへ直接Spawnしないこと。
class ObjectFactory final
{
public:
    // 例: "StoneWall"という名前で、形・材質・倍率・描画順などをまとめて覚える。
    // 名前の重複はエラー。別の登録をうっかり上書きして壁が家に化けるのを防ぐ。
    void Register(std::string prefabName, ObjectDesc defaults);

    // ひな形のコピーを返す。ここで位置や角度を変えても、登録した元は変わらない。
    // Mesh/Textureの実体はshared_ptrで共有するので、形状データまでは複製しない。
    [[nodiscard]] ObjectDesc MakeDescription(std::string_view prefabName) const;

    // 位置だけ変えて置く、よく使う入口。登録した倍率・回転・描画設定はそのまま。
    [[nodiscard]] ObjectId Spawn(World& world, std::string_view prefabName,
        const DirectX::XMFLOAT3& position) const;

    bool Contains(std::string_view prefabName) const;
    bool Remove(std::string_view prefabName);
    void Clear() noexcept;
    std::size_t GetCount() const noexcept { return prefabs_.size(); }

private:
    // 今は読み込み済みのひな形を持つ。登録が残る間は参照先のGPU資産も生きる。
    // シーン退出でClearする。将来の距離ロードでは未ロードのAsset IDによる定義も必要になる。
    std::map<std::string, ObjectDesc, std::less<>> prefabs_;
};
