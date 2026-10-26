# DESI:Re

**C++20 / DirectX 12で制作した、小規模ゲーム向けの学習用エンジンです。**

3Dモデルと画像の描画、シーン切替、CPU/GPUリソース管理、文字マップによる配置、衝突判定、ボーンアニメーションを組み合わせて使えます。
動作例として、自動生成された迷路を敵から逃げながら進む、1ステージの脱出ゲームを同梱しています。

DESIは **Designer / Engineer / Scripter / Integrator** に由来します。

## 同梱ゲーム

起動ロゴの後、迷路へ入ります。水色の床がゴールです。

- 迷路は穴掘り法を基に生成し、追加で通路にループを作ります。
- 敵はランダムに配置され、Find / Chaseの2状態で行動します。
- プレイヤーの推定位置に向かい、グリッド上の最短経路をBFSで求めて移動します。
- 視界の角度・距離と壁の遮蔽で発見を判定し、追跡中は位置推定の間隔・誤差を小さくします。
- 敵に触れるとGame Over、ゴールへ入ると「To be continued?」のPNGを表示します。

| 操作 | キー |
|---|---|
| 移動 | W / A / S / D |
| 視点の回転 | 矢印キー |
| プレイ中のやり直し | F2。同じ迷路を保ち、敵を再配置 |
| ロゴのスキップ | Enter / Space |
| 結果画像から終了 | Enter / Space |
| 終了 | Esc |

現在のStage1では、マウス移動を視点操作に使いません。

## 主な実装

| 分野 | 内容 |
|---|---|
| ゲームループ | Win32ウィンドウ、入力、時間管理、1/60秒刻みの固定更新 |
| 描画 | DirectX 12による3Dメッシュ・画像描画、材質、テクスチャ、深度、簡単な光源処理 |
| 画面表現 | 内部解像度の変更、拡大表示、簡易CRTの走査線・周辺減光 |
| シーン | 名前による登録と切替、シーン終了時のリソース返却、PNG紙芝居 |
| ワールド | Transform、オブジェクト生成、配置レイヤー、描画レイヤーとレイヤー内順序 |
| マップ | 文字列・テキストファイル・生成結果を共通マップへ渡し、記号に応じたオブジェクトを配置 |
| CPUメモリ | 4 KiBページ単位のアロケータ、アラインメント対応、System / Sceneアリーナ、オブジェクトプール |
| リソース | ファイルの共有キャッシュ、参照に応じた寿命管理、未使用キャッシュの回収 |
| GPUメモリ | D3D12MAとの接続、用途別の使用量・予算管理、GPU処理完了を待ってから解放 |
| モデル・アニメ | ufbxを使うFBX読込、ボーン姿勢更新、GPUスキニング、再生・ループ・クリップ切替 |
| 衝突 | AABB / Sphere、AutoFit、壁で止まる移動、TriggerのEnter / Stay / Exit |
| デモAI | Find / Chase、壁越しの視認防止、誤差付きの位置推定、経路探索、Idle / Walking切替 |

CPUメモリは独自の管理領域を4 KiB単位で切り分けます。プロセス全体のメモリや、すべてのSTL確保を独自アロケータへ置き換えているわけではありません。
自動回収はRAII・参照カウントと未使用キャッシュの回収によるもので、トレーシングGCではありません。

GPU側のヒープ確保・サブアロケーションにはD3D12MAを使用し、エンジン側で用途分類、所有権、統計を扱います。
用途別の予算は管理対象リソースの確保を制限するもので、OS全体のVRAM使用量を固定する機能ではありません。現版のRenderTargetは使用量を計測しますが、ほかの用途と同じ厳密な予算超過チェックの対象にはしていません。
FBXの解析とアニメーションのベイク・キー評価にはufbxを使用し、エンジン側で材質・描画データへの変換、階層行列、スキニング、再生制御へつないでいます。

## 構成

```text
Desire.sln
Desire/
├ main.cpp          シーン登録と起動
├ EngineSelf/       エンジンループ・ウィンドウ・入力・タイマー
├ MemorySystem/     CPUメモリの確保と寿命管理
├ Resource/         ファイル・画像・メッシュの読み込み
├ Render/           DX12描画・GPUリソース・スプライト
├ PostEffect/       内部描画先と拡大・CRT処理
├ Camera/           カメラと追従・一人称操作
├ World/            Transform・配置・文字マップ
├ Physics/          衝突・Trigger・キャラクター移動
├ Model/            FBXの接続・アニメーション・モデルの配置
├ Scene/            シーン切替・イベント・紙芝居
├ Game/             同梱ゲーム・迷路生成・敵AI
├ Assets/           モデル・画像・マップ
└ ThirdParty/       D3D12MA・ufbx
```

フォルダ・プロジェクト名はDesire、画面上のエンジン名はDESI:Reです。
ゲーム固有の進行や敵AIは主にGame配下、再利用する仕組みは各エンジン機能へ分けています。

## ビルドと起動

対象はWindowsのx64環境です。DirectX 12を利用できるGPUとドライバーが必要です。

1. Visual Studio 2022で「C++によるデスクトップ開発」、MSVC v143、Windows SDKを用意します。
2. `Desire.sln` を開きます。
3. 構成を **Release / x64** にしてビルドします。
4. Visual Studioから実行する場合は、デバッグの作業ディレクトリを `$(ProjectDir)` にします。
5. 実行します。`Assets` 内のFBXと画像が必要です。

プロジェクトはC++20を使用します。追加ライブラリのソースはThirdPartyに同梱されており、別途パッケージマネージャーから取得する手順はありません。

`ufbx.c` は `Model/UfbxImplementation.cpp` から1回だけ取り込んでいます。ufbx.cを別のコンパイル対象として追加しないでください。

配布版は、Releaseの実行ファイルとAssetsを同じフォルダへ置きます。
現在は実行ファイルの位置ではなく、**作業ディレクトリのAssets**を基準に読み込むため、そのフォルダを作業ディレクトリとして起動してください。
既定の迷路設定では `Assets/Maps/generated-stage01.map` に生成結果を書き込むので、書き込み可能な場所へ展開します。

```text
配布フォルダ/
├ 実行ファイル.exe
├ Assets/
├ README.md
├ THIRD_PARTY_NOTICES.md
└ licenses/
   ├ D3D12MA-LICENSE.txt
   ├ ufbx-LICENSE.txt
   └ DirectXMath-LICENSE.txt
```

Visual C++のランタイムが必要な環境では、Microsoftの対応するx64再頒布可能パッケージを利用してください。
必要なランタイムの版はビルドに使ったツールセットと対応させます。[Microsoftの案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)

## 設定とデータ

- 迷路のサイズ・追加通路数・結果画面の行き先：`Desire/main.cpp` の `MazeStageSettings`。
- 敵の速度・視界・推定誤差・更新間隔：`Desire/Game/AI/EnemySettings.h`。
- 起動時の内部解像度・CRT：`Desire/EngineSelf/Engine.h`。
- 結果画像：`Desire/Assets/Story/GameOver/001.png` と `Story/Ending/001.png`。

既定の内部解像度は320×180、CRTはFastです。
ゴールの見た目は水色の床、判定は別の透明なTriggerです。
開始用の紙芝居シーンも登録できますが、現在の起動設定は「ロゴ→Stage1」です。

## 使用ライブラリ・基盤

| 名前 | 使用箇所 | ライセンス・版 |
|---|---|---|
| [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) | GPUメモリ確保とサブアロケーション | MIT。同梱ヘッダー表記3.2.0 |
| [ufbx](https://github.com/ufbx/ufbx) | FBX解析、アニメーションのベイク・キー評価 | v0.23.0。Alternative AのMITを選択 |
| [DirectXMath](https://github.com/microsoft/DirectXMath) | ベクトル・行列演算 | MIT。Windows SDKに含まれる版を利用 |

Win32、Direct3D 12、DXGI、Windows Imaging Component、D3DCompiler、Visual C++ランタイムはMicrosoft製基盤として利用します。
これら全体をエンジンのMITライセンスで再許諾するものではありません。各コンポーネントの適用条件に従います。

開発ツールはVisual Studio、モデル制作はBlenderです。

## 現在の範囲

小規模なゲーム制作と学習・ポートフォリオを目的とする実装です。
距離ストリーミング、LOD、フラスタムカリング、シャドウマップ、GUIエディタは現版の実装範囲に含めていません。
CRTはNone / Fastに対応し、Fullは未実装です。アニメーションのクリップ切替は可能ですが、クロスフェードは未実装です。

FBXは現在のローダーが対応する形式で使用します。線形スキニングと外部画像への相対パスを前提とし、埋め込み画像やモーフには対応していません。

CPU管理の一部には排他制御を設けていますが、Scene・World・描画の操作はメインスレッドで行います。

## 使用ライブラリのライセンス

D3D12 Memory Allocator、ufbx、DirectXMathはMITライセンスの条件で使用しています。ufbxは選択可能なライセンスのうちAlternative AのMITを選択しています。
各ライブラリの著作権表示・ライセンス本文は、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)とlicensesフォルダにまとめています。

ソースまたは実行ファイルを再配布する場合も、対応するライブラリの著作権表示とライセンス本文を保持してください。
動画説明欄のクレジットだけで、配布物内のライセンス表記を置き換えることはできません。[MIT License本文](https://opensource.org/license/mit)

ここでのMIT表記は使用ライブラリについてのものです。エンジン本体・ゲームコードや、モデル、テクスチャ、ロゴ、画像、音声、動画などの素材へ、一括でMITライセンスを適用する表記ではありません。
