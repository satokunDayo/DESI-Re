# DESI:Re — GPU基盤 01

エンジン名は **DESI:Re**。Designer / Engineer / Scripter / Integrator。
`:Re` は ReDive の Re。画面表示には `DESI:Re`、C++識別名・フォルダー・ファイル名には `DESIRe` を使います。

今回は **PNG → CPUの画素 → GPU画像 → 画面表示 → 安全な解放・使用量表示** をつなぎます。
今までのCore01（Title / Stage01 / Stage02 / Result、Event、Paused）から続く教材です。
実プロジェクトは編集していません。自分で書くコードと比較するための更新版を用意しました。

## 最初にすること

1. [ファイル配置](ファイル配置.md)で追加・更新場所を確認。
2. [クラスと処理の流れ](クラスと処理の流れ.md)を読む。
3. 新規ファイルを `GpuMemory.h` → `.cpp` の順で入力。以降の順番も配置表に記載。
4. `updated-reference` の既存ファイル更新を反映し、Assetsと外部ライブラリを配置。
5. C++20 / x64 / Windowsサブシステムでビルド。

全ファイルを反映するまではビルドが通りません。今回はGPU画像の所有とSceneへの接続が相互に関係するためです。
外部ライブラリD3D12MAのソースは手入力せず、そのままコピーしてください。

## 完成後の見え方

- ウィンドウタイトルの先頭が `DESI:Re`。
- Titleの中央にチェック柄画像。
- Enter → Stage01。背景画像と、その画像を共有する小さい画像を表示。
- 最初のEvent中は回転停止。Escでスキップすると小さい画像が回る。
- PでPause / Resume。EでEvent。EnterでResult、さらにEnterでTitle。
- TitleからF2ならStage02へ直接移動。
- Resultには画像を置いていないので、GPU Texの使用量が0になる。
- CPUの元PNGキャッシュはResourceManagerの既存規則で回収。GPU画像は最後の所有者が手放した時点で回収。

### 描画依頼の書き方

シーンの `OnEnter` で1回ロードし、メンバーに保持します。

```cpp
image_ = context.textures->Load("Textures/checker.png", ArenaType::Scene);
```

`GetPresentation()` で描画内容を返します。

```cpp
ScenePresentation view;
SpriteDraw picture;
picture.texture = image_;
picture.x = 320;
picture.y = 180;
picture.width = 640;
picture.height = 360;
picture.rotationDegrees = 0;
picture.layer = 0;
picture.order = 10;
view.sprites.push_back(picture);
return view;
```

退出時は `image_.reset()`。コピーされた `GpuTextureHandle` が残っていれば画像は生存します。
`SpriteDraw` をコピーして別の位置に置いても、画像のGPUメモリは増えません。
毎フレームLoadを呼ぶ必要はありません。

## 今回の範囲

実装したもの：CPU Arenaを使うWIC画像展開、D3D12MAを使うGPU確保、RGBA8画像転送、ポイントサンプリング、透明画像、座標・サイズ・回転・色・UV、画像の描画順、共有、最後の所有者による回収、GPU予算の確認、Sceneとの接続。

まだ追加するもの：3Dモデル・深度・光・カメラ、GpuMesh、スケルタルアニメーション、UI文字・フォーカス・ボタン、操作・衝突、オブジェクト/Prefab/マップ配置、音、距離プリロード・LRU・LOD・遠距離AI軽量化。

画像は現在「画面上の座標」で描きます。3D空間の看板やXY平面に固定するワールドSpriteは、カメラと共通オブジェクト描画を接続するときに追加します。
画像のlayer/orderを、3Dの奥行き判定やマップ配置レイヤーに流用しません。

## 同期と回収

この版の `UploadImage` と `DrawFrame` は、同じDirect Queueを使い、各呼び出しでFence完了を待ちます。
転送中のUPLOADと画像、描画中の画像はRendererが一時所有します。完了後にその所有を外すため、呼び出し元が手放した画像を安全に返せます。
**非同期ストリーミングやFence別の遅延解放キューは、この版にはまだありません。** 毎フレーム待機を取り除く段階で、画像・UPLOAD・SRVスロットの寿命をFence値に結び付けます。
ゲーム側は今後も同じ画像ハンドルを使う設計です。

画像ごとのSRVヒープは理解しやすさを優先した初版です。大量Spriteの描画には、共通SRVヒープ・バッチ化を追加します。
Mipmap、圧縮テクスチャ、sRGB/リニア色空間変換、画像のEXIF向き・カラープロファイル、画像内アニメーションは未対応です。WICの先頭フレームをRGBA8として扱います。

## メモリ表示の読み方

| 表示/API | 意味 |
|---|---|
| CPU | 既存Scene Arenaの予約済みブロック合計。プロセス全体のRAMではない |
| GPU Tex | D3D12MAの画像Allocation合計 / エンジンの画像目標 |
| Upload | GPU転送用Allocation合計。同期転送が戻った後は0 |
| Heap | D3D12MAが持つヒープ。空き部分も含む |
| OS(local) | OSのローカルセグメント使用量 / 予算。GPUの専用VRAM容量とは別 |
| GetGpuStats().allocationBytes[2] | SwapChainの2枚をデバイス要求サイズで見積もったRT合計 |

CPUの初期設定はSystem 64 MiB + Scene 192 MiBを維持しています。
GPUのカテゴリ目標はTexture 32 MiB、Mesh 16 MiB、RT 16 MiB、Upload 16 MiB。
これは用途別の管理値で、物理VRAMを事前にその容量で分割・予約する設定ではありません。
Texture/Mesh/Uploadは、実際のAllocationサイズで目標を超えた確保を返却して失敗を通知します。メモリ不足時のLRU回収はまだ行いません。
RTはリサイズを止めない監視用目標です。深度バッファはまだ未実装なので、今回のRTはバックバッファ2枚のみ。

HeapにはTexture/Mesh/Uploadの確保領域が含まれます。OS使用量にも同じ領域が含まれます。**これらは足し算しません。**
UPLOADはGPUから見えるシステムメモリです。UMAではCPU/GPUが物理メモリを共有するため、表示をそのまま専用VRAM量と解釈しません。
D3D12MAには4 MiBの希望ブロックサイズを設定しました。空ブロックが再利用のため残る場合があり、Texが0でもHeapが0とは限りません。
制御構造・STL・WIC内部作業・ドライバー内部メモリはCPU Arena予算の外です。

## 追加ライブラリ

D3D12MemoryAllocator **v3.2.0**のみ。MITライセンス原文を同梱しました。
WIC、DirectX、HLSLコンパイラーはWindows SDKの基盤を使います。
取得元とSHA-256は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) に記録します。

技術資料：[Microsoftの画像転送](https://learn.microsoft.com/en-us/windows/win32/direct3d12/upload-and-readback-of-texture-data)、[D3D12MAの所有権](https://gpuopen-librariesandsdks.github.io/D3D12MemoryAllocator/html/quick_start.html)、[公式リリースv3.2.0](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator/releases/tag/v3.2.0)。
