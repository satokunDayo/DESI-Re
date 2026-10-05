#include "World/MapData.h"
#include "Resource/ResourceManager.h"
#include <stdexcept>
#include <utility>

namespace
{
    bool IsSymbol(char c) noexcept
    { const auto byte = static_cast<unsigned char>(c); return byte >= 32 && byte <= 126; }
}
void SymbolTable::Insert(char symbol, MapSymbol definition)
{
    if (!IsSymbol(symbol)) throw std::invalid_argument("Map symbols must be printable ASCII.");
    if (!entries_.emplace(symbol, std::move(definition)).second)
        throw std::invalid_argument("Duplicate map symbol: " + std::string(1, symbol));
}
void SymbolTable::Register(char symbol, std::string prefab,
    std::optional<int> renderLayer, std::optional<int> orderInLayer)
{
    if (prefab.empty()) throw std::invalid_argument("Use Ignore for an empty map cell.");
    Insert(symbol, { std::move(prefab), renderLayer, orderInLayer });
}
void SymbolTable::Ignore(char symbol) { Insert(symbol, {}); }
const MapSymbol* SymbolTable::Find(char symbol) const noexcept
{ const auto it = entries_.find(symbol); return it == entries_.end() ? nullptr : &it->second; }

MapGrid MapGrid::FromRows(std::span<const std::string_view> rows)
{
    if (rows.empty() || rows.front().empty()) throw std::invalid_argument("Map needs non-empty rows.");
    const auto width = rows.front().size();
    if (width > MaxCells || rows.size() > MaxCells / width)
        throw std::invalid_argument("Map exceeds the cell limit.");
    MapGrid result;
    result.rows_.reserve(rows.size());
    for (std::size_t row = 0; row < rows.size(); ++row)
    {
        if (rows[row].size() != width)
            throw std::invalid_argument("Map row " + std::to_string(row + 1) + " has a different width.");
        for (std::size_t column = 0; column < width; ++column)
            if (!IsSymbol(rows[row][column]))
                throw std::invalid_argument("Map needs printable ASCII at row " + std::to_string(row + 1) +
                    ", column " + std::to_string(column + 1) + ".");
        result.rows_.emplace_back(rows[row]);
    }
    return result;
}
MapGrid MapGrid::FromText(std::string_view text)
{
    if (text.size() > MaxTextBytes) throw std::invalid_argument("Map text is too large.");
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::vector<std::string_view> rows;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        auto row = text.substr(0, end);
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        rows.push_back(row);
        // 末尾の改行1個は余分な空行にしない。途中の空行や改行2個は検査で止める。
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
        if (rows.size() > MaxCells) throw std::invalid_argument("Map has too many rows.");
    }
    return FromRows(rows);
}
MapGrid MapGrid::FromFile(ResourceManager& resources, const std::filesystem::path& relativePath)
{
    try
    {
        // ここでは元バイト列を借りる。Parse後は地図が文字を所有し、Handleは手放せる。
        const auto asset = resources.AcquireBinary(relativePath);
        const auto bytes = asset->GetBytes();
        return FromText({ reinterpret_cast<const char*>(bytes.data()), bytes.size() });
    }
    catch (const std::exception& error)
    { throw std::runtime_error("Map file " + relativePath.string() + ": " + error.what()); }
}
char MapGrid::At(std::size_t column, std::size_t row) const { return rows_.at(row).at(column); }
