#include "MazeAlgorithmSatokunSpecial.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <utility>


// 無名名前空間 (このファイルの中だけで使う便利な定数まとめ)

namespace
{
    // 上、右、下、左 の4方向へ進むための座標の増減値。
    // 例えば d=1 (右) のとき、X座標に dx[1](=1) を足し、Y座標に dy[1](=0) を足す。
    constexpr int dx[] = { 0, 1, 0, -1 };
    constexpr int dy[] = { -1, 0, 1, 0 };
}


// コンストラクタ (初期化と安全チェック)

MazeAlgorithmSatokunSpecial::MazeAlgorithmSatokunSpecial(
    char wall, char path, char player, int w, int h,
    std::filesystem::path file, char goal)
    : wallChar(wall), pathChar(path), playerChar(player), goalChar(goal),
    width(w), height(h), filename(std::move(file))
{
    // サイズの安全チェック。マイナスの値や巨大すぎる値でメモリを食いつぶさないようにする。
    if (width < 5 || height < 5 || width > MaxSide || height > MaxSide)
        throw std::invalid_argument("Maze dimensions must be between 5 and 101.");

    // 穴掘り法などの迷路生成は、壁と道が交互に並ぶため、幅と高さが奇数である必要あり
    // もし偶数が指定された場合は、自動的に +1 して奇数に補正

    if (width % 2 == 0) ++width;
    if (height % 2 == 0) ++height;

    // 迷路に使う文字（壁、道、プレイヤー、ゴール）が正常かどうかのチェック
    const std::array<char, 4> symbols{ wallChar, pathChar, playerChar, goalChar };
    for (std::size_t i = 0; i < symbols.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(symbols[i]);
        // 制御文字（改行やタブなど）や、文字化けする値でないか（画面に表示できる文字か）を確認
        if (c < 32 || c > 126)
            throw std::invalid_argument("Maze symbols must be printable ASCII.");

        // 文字が被っていないか確認（壁も道も#だとゲームにならない）
        for (std::size_t j = i + 1; j < symbols.size(); ++j)
            if (symbols[i] == symbols[j])
                throw std::invalid_argument("Maze symbols must be distinct.");
    }
}


//ファイル名の変更
// ゲーム起動時に1回だけ「迷路職人」を作る（重い処理はここだけ）
//MazeAlgorithmSatokunSpecial mazeGen('#', ' ', 'P', 31, 31);

// --- 1階に進んだ時 ---
//mazeGen.setFilename("stage1.txt"); // 職人に「次はstage1.txtという名前でよろしく」と指示する
//mazeGen.generate();                // 迷路を作らせる
// --- 2階に進んだ時 ---
//mazeGen.setFilename("stage2.txt"); // 同じ職人に「次はstage2.txtに変えてね」と指示する
//mazeGen.generate();                 // 迷路を作らせる


void MazeAlgorithmSatokunSpecial::setFilename(const std::filesystem::path& newFilename)
{
    filename = newFilename;
}

// 後から迷路のサイズを変更する関数
void MazeAlgorithmSatokunSpecial::setSize(int w, int h)
{
    // まず受け取った値を確認する。
    // ダメならここで止めるので、今までの設定は壊れない。
    if (w < 5 || h < 5 || w > MaxSide || h > MaxSide)
        throw std::invalid_argument(
            "Maze dimensions must be between 5 and 101.");

    // 手元の引数を奇数にそろえる。
    if (w % 2 == 0) ++w;
    if (h % 2 == 0) ++h;

    // 問題ないことが確定してから、本体の設定を書き換える。
    width = w;
    height = h;
}


// 迷路生成のメイン処理

void MazeAlgorithmSatokunSpecial::generate(int extraHoles)
{
    if (extraHoles < 0)
        throw std::invalid_argument("extraHoles must be nonnegative.");

    // 1. マップ全体を「壁」で塗りつぶす。
    maze.assign(height, std::string(width, wallChar));

    // 2. 座標(1, 1)からスタートして、道を掘っていく。（穴掘り法）
    dig(1, 1);

    // 3. 行き止まりをなくす（ループ状の道を作るため）。
    removeDeadEnds();

    // 4. 指定された数だけ、さらに壁をぶち抜いてショートカットを作る。
    addExtraHoles(extraHoles);

    // 地形を変える処理が全部終わってから、開始位置とゴールを決める。
    // （途中でゴールを置くと、壁ぶち抜きの対象になって消えたりするのを防ぐため）
    maze[1][1] = playerChar;
    placeGoal();

    // 5. ファイル名が指定されていれば保存する。
    if (!filename.empty())
    {
        // 毎回ファイルを新規作成(trunc)して上書き保存。どんどんファイルが巨大化するのを防ぐ。
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot create maze file: " + filename.string());

        file << getMapText();
        file.close();

        if (!file) throw std::runtime_error("Cannot write maze file: " + filename.string());
    }
}


// 穴掘り法（道を作るアルゴリズム）

void MazeAlgorithmSatokunSpecial::dig(int x, int y)
{
    // 【C++特有の工夫】
    // 通常の穴掘り法は「関数が自分自身を呼び出す（再帰呼び出し）」で作りますが、
    // 迷路が巨大になると「呼び出しスタック」の限界を超えてソフトが強制終了してしまいます。
    // それを防ぐため、std::vectorを「戻る場所のメモ帳（スタック）」として使っています。
    std::vector<Cell> stack;

    // メモリの再確保が起きないよう、あらかじめ必要な分だけ予約しておく。
    stack.reserve(static_cast<std::size_t>(width / 2) * (height / 2));

    // 最初のマスを道にして、メモ帳に記録。
    maze[y][x] = pathChar;
    stack.push_back({ x, y });

    // メモ帳が空っぽになる（すべての道を掘り尽くす）までループ。
    while (!stack.empty())
    {
        const auto current = stack.back(); // 現在位置を取得

        // 4方向（0:上, 1:右, 2:下, 3:左）の順番をランダムにシャッフルする。
        std::array<int, 4> dirs{ 0, 1, 2, 3 };
        std::shuffle(dirs.begin(), dirs.end(), random_);

        bool advanced = false; // 今回のループで掘り進めたかどうかのフラグ

        // ランダムに決めた順番で、掘れる方向を探す。
        for (int d : dirs)
        {
            // 穴掘り法では「2マス先」をチェックする。（1マス先は道と道の間にある壁）
            const int nx = current.x + dx[d] * 2;
            const int ny = current.y + dy[d] * 2;

            // 迷路の外周（枠）を壊さないようにチェック。
            if (nx <= 0 || nx >= width - 1 || ny <= 0 || ny >= height - 1)
                continue;

            // 2マス先がすでに「道」だったら、そこには繋げない（壁のままにしておく）。
            if (maze[ny][nx] != wallChar) continue;

            // 掘れる条件を満たしたので、1マス先（間にある壁）と、2マス先を「道」にする。
            maze[current.y + dy[d]][current.x + dx[d]] = pathChar;
            maze[ny][nx] = pathChar;

            // 新しい現在位置をメモ帳に追加して、ループをやり直す。
            stack.push_back({ nx, ny });
            advanced = true;
            break;
        }

        // 4方向すべてチェックして、もうどこにも掘れなかったら「行き止まり」。
        // メモ帳から現在の位置を消して、1歩前の分岐点に戻る。
        if (!advanced) stack.pop_back();
    }
}


//行き止まりをなくす処理

void MazeAlgorithmSatokunSpecial::removeDeadEnds()
{
    bool changed;
    do
    {
        changed = false;
        // 迷路全体をスキャンする。
        for (int y = 1; y < height - 1; ++y)
            for (int x = 1; x < width - 1; ++x)
            {
                if (maze[y][x] != pathChar) continue;

                // 周囲4方向を見て、繋がっている「道」がいくつあるか数える。
                int paths = 0;
                for (int d = 0; d < 4; ++d)
                    if (maze[y + dy[d]][x + dx[d]] == pathChar) ++paths;

                // 繋がっている道が1つしかない ＝ 行き止まり！
                if (paths != 1) continue;

                // 行き止まりの周囲の壁をランダムな方向で壊し、別の道と繋ぐ。
                std::array<int, 4> dirs{ 0, 1, 2, 3 };
                std::shuffle(dirs.begin(), dirs.end(), random_);

                for (int d : dirs)
                {
                    const int nx = x + dx[d] * 2;
                    const int ny = y + dy[d] * 2;
                    if (nx <= 0 || nx >= width - 1 || ny <= 0 || ny >= height - 1)
                        continue;

                    // その方向の「壁」の向こう側が「道」であれば、壁を壊して貫通させる。
                    if (maze[y + dy[d]][x + dx[d]] == wallChar && maze[ny][nx] == pathChar)
                    {
                        maze[y + dy[d]][x + dx[d]] = pathChar; // 壁を壊す
                        changed = true; // 迷路の形が変わったので、再スキャンが必要
                        break;
                    }
                }
            }
        // 行き止まりが1つもなくなるまで、この処理を繰り返す。
    } while (changed);
}


//追加で壁をぶち抜く（ショートカットを作る）処理

void MazeAlgorithmSatokunSpecial::addExtraHoles(int count)
{
    if (count == 0) return;

    // ぶち抜く候補となる「壁の座標」をリストアップする。
    std::vector<Cell> candidates;
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
        {
            // 道は対象外。壁だけを見る。
            if (maze[y][x] != wallChar) continue;

            // その壁の「左右」または「上下」が両方とも道になっているかチェック。
            const bool horizontal = maze[y][x - 1] == pathChar && maze[y][x + 1] == pathChar;
            const bool vertical = maze[y - 1][x] == pathChar && maze[y + 1][x] == pathChar;

            // 既存の通路同士を隔てている壁だけを候補にする。
            // （これ以外の壁を壊すと、外に出られちゃったり、孤立した部屋ができたりする）
            if (horizontal || vertical) candidates.push_back({ x, y });
        }

    // 候補リストをランダムにシャッフルする。
    std::shuffle(candidates.begin(), candidates.end(), random_);

    // 要求された回数 (count) と、壊せる壁の数のうち、少ない方を上限にする。
    const auto limit = (std::min)(static_cast<std::size_t>(count), candidates.size());

    // 上限の数だけ、候補に挙がった壁を道に変える（ぶち抜く）。
    for (std::size_t i = 0; i < limit; ++i)
        maze[candidates[i].y][candidates[i].x] = pathChar;
}


//ゴールを一番遠い場所に配置する処理

void MazeAlgorithmSatokunSpecial::placeGoal()
{
    // 幅優先探索 (BFS) というアルゴリズムを使って距離を測る。
    // スタート地点から水面を波紋が広がるように調べていくため、
    // extraHoles で複数のルート（ループ）ができていても、正確な「最短歩数」を測ることができる。

    // 各マスまでの距離を保存するマップ。-1は「まだ調べていない」状態。
    std::vector<std::vector<int>> distance(height, std::vector<int>(width, -1));
    std::queue<Cell> queue; // 調べる予定のマスを入れる順番待ちリスト

    Cell goal{ 1, 1 }; // ゴール候補（初期値はスタート位置）
    int farthest = 0;  // これまで見つけた最大の歩数

    // スタート位置(1,1)の距離を0歩にして、リストに入れる。
    distance[1][1] = 0;
    queue.push({ 1, 1 });

    while (!queue.empty())
    {
        const auto current = queue.front(); // リストの先頭を取り出す
        queue.pop();

        for (int d = 0; d < 4; ++d)
        {
            const int nx = current.x + dx[d];
            const int ny = current.y + dy[d];

            // 迷路の範囲外なら無視。
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            // 壁である、もしくは既に距離を調べ終わったマスなら無視。
            if (maze[ny][nx] == wallChar || distance[ny][nx] >= 0) continue;

            // 「今のマスの歩数 + 1」が次のマスの歩数になる。
            const int steps = distance[current.y][current.x] + 1;
            distance[ny][nx] = steps;
            queue.push({ nx, ny }); // 次に調べるマスとしてリストに追加

            // もしこれまでの最大歩数より遠かったら、ここを新たなゴール候補にする。
            if (steps > farthest)
            {
                farthest = steps;
                goal = { nx, ny };
            }
        }
    }

    // どこにも行けなかった（スタート地点が四方を壁に囲まれているなど）場合はエラー。
    if (farthest == 0) throw std::runtime_error("Maze has no reachable goal cell.");

    // 一番遠かったマスをゴールにする。
    maze[goal.y][goal.x] = goalChar;
}


//迷路データを文字列化 / 出力する処理

std::string MazeAlgorithmSatokunSpecial::getMapText() const
{
    std::string text;
    // メモリの無駄遣いを防ぐため、最終的に必要な文字数を計算して予約しておく。
    // (幅 + 改行文字1つ) × 高さ
    text.reserve(static_cast<std::size_t>(width + 1) * height);

    for (const auto& row : maze)
    {
        text += row;
        text += '\n'; // 1行ごとに改行を入れる
    }
    return text;
}

void MazeAlgorithmSatokunSpecial::print() const
{
    // 生成したテキストをそのままコンソール画面に出力。
    std::cout << getMapText();
}