#pragma once
// 文字の地図と「この記号は何を置くか」の約束。ここではまだWorldに何も生まない。
#include "Transform.h"
#include <filesystem>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
class ResourceManager;

// 空のprefabは明示的な無視。未登録記号を勝手に空白扱いすると誤字に気付けない。
struct MapSymbol
{
    std::string prefab;
    std::optional<int> renderLayer;
    std::optional<int> orderInLayer;
};
class SymbolTable final
{
public:
    void Register(char symbol, std::string prefab,
        std::optional<int> renderLayer = {}, std::optional<int> orderInLayer = {});
    void Ignore(char symbol);
    const MapSymbol* Find(char symbol) const noexcept;
private:
    void Insert(char symbol, MapSymbol definition);
    std::map<char, MapSymbol> entries_;
};

// 入力をどれにしても同じ長方形の地図になる。文字列はコピーして持つので一時変数でもよい。
class MapGrid final
{
public:
    static constexpr std::size_t MaxCells = 1024 * 1024;
    static constexpr std::size_t MaxTextBytes = 4 * 1024 * 1024;
    static MapGrid FromRows(std::span<const std::string_view> rows);
    static MapGrid FromRows(std::initializer_list<std::string_view> rows)
    { return FromRows(std::span<const std::string_view>(rows.begin(), rows.size())); }
    // LF／CRLFと先頭のUTF-8 BOMに対応。空白はマスなのでTrimしない。
    static MapGrid FromText(std::string_view text);
    // Assets基準の既存ResourceManagerを使う。別のファイルキャッシュを増やさない。
    static MapGrid FromFile(ResourceManager& resources, const std::filesystem::path& relativePath);
    std::size_t Width() const noexcept { return rows_.empty() ? 0 : rows_.front().size(); }
    std::size_t Height() const noexcept { return rows_.size(); }
    char At(std::size_t column, std::size_t row) const;
private:
    std::vector<std::string> rows_;
};

enum class GridPlane { XY, XZ };
struct MapLayer
{
    std::string name;
    int placementLayer = 0; // 分類番号。描画順や高さへは自動変換しない。
    MapGrid grid;
    SymbolTable symbols;
    GridPlane plane = GridPlane::XZ;
    DirectX::XMFLOAT2 cellSize{ 1, 1 }; // 格子の間隔。Mesh自体の倍率とは別。
    Transform transform; // 原点・回転・均一スケール。生成時に各物へ合成する。
    // 未指定なら原型の設定を使う。記号側に指定があればそちらが優先。
    std::optional<int> renderLayer;
    std::optional<int> orderInLayer;
};
struct MapData { std::vector<MapLayer> layers; };
