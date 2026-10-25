#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <vector>

// 文字の迷路を作る係。
// 壁や床の描画、当たり判定、シーン切り替えは担当しない。
class MazeAlgorithmSatokunSpecial final
{
public:
    // 外周の壁も含めた、一辺の最大マス数。
    // 無制限に大きな迷路を作らないための上限。
    static constexpr int MaxSide = 101;

    // 壁・通路・プレイヤー・ゴールの記号と、生成サイズを指定する。
    // 記号はそれぞれ異なる、表示可能なASCII文字を使う。
    // 幅と高さは5～101。偶数なら+1して奇数にそろえる。
    // filenameが空ならファイルには保存しない。
    MazeAlgorithmSatokunSpecial(
        char wallChar,
        char pathChar,
        char playerChar,
        int width,
        int height,
        std::filesystem::path filename = {},
        char goalChar = 'G');

    // 次にgenerate()したときの保存先を変更する。
    // 空のパスを渡せば、ファイル保存を無効にできる。
    void setFilename(const std::filesystem::path& newFilename);

    // 次に生成するサイズを変更する。
    // この呼び出しだけでは、生成済みの迷路は変わらない。
    // 幅と高さは5～101。偶数なら+1する。
    void setSize(int w, int h);

    // 古い迷路を上書きして、新しい迷路を生成する。
    // 穴掘り → 行き止まり解消 → 追加の穴 → プレイヤーとゴール配置。
    // extraHolesは、さらにショートカットを作る数。
    // 壊せる壁が少なければ、その分だけ開ける。
    // 保存先が指定されていれば、生成結果をファイルへ上書き保存する。
    void generate(int extraHoles = 0);

    // 現在の迷路を、改行区切りの文字列としてコピーして返す。
    // この戻り値をMapGrid::FromText()へ渡せる。
    std::string getMapText() const;

    // 現在の迷路を標準出力へ表示する。確認用。
    void print() const;

private:
    // 迷路上のマス座標。3D空間の座標とは別物。
    struct Cell
    {
        int x;
        int y;
    };

    // 指定位置から、2マスずつ進んで通路を掘る。
    void dig(int x, int y);

    // 行き止まりから別の通路へつながる壁を開ける。
    void removeDeadEnds();

    // 通路同士をつなげられる壁から、追加で開ける場所を選ぶ。
    void addExtraHoles(int count);

    // スタートから各マスまでの最短歩数を幅優先探索で調べ、
    // 到達できるマスのうち、最も歩数が多い場所へゴールを置く。
    void placeGoal();

    // 迷路に書き込む記号。
    char wallChar;
    char pathChar;
    char playerChar;
    char goalChar;

    // 次回生成に使う幅と高さ。奇数に補正して保持する。
    int width;
    int height;

    // 保存先。空なら保存しない。
    std::filesystem::path filename;

    // 生成済みの迷路本体。
    // maze[y]が1行、maze[y][x]がその行の1マス。
    // generate()を呼ぶたびに、新しい内容へ作り直す。
    std::vector<std::string> maze;

    // この生成器が生きている間、同じ乱数エンジンを使い回す。
    // 掘るたびに初期化し直す必要はない。
    std::mt19937 random_{ std::random_device{}() };
};