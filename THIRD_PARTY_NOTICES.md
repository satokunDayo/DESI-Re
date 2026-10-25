# Third-party notices — DESI:Re

DESI:Reで利用するライブラリの著作権表示とライセンス本文です。
以下のMIT表記は各ライブラリへ適用されます。この文書はDESI:Re本体・ゲームコード・素材のライセンスを設定するものではありません。

## 使用ライブラリ

| ライブラリ | 用途 | 使用版 | 選択したライセンス |
|---|---|---|---|
| D3D12 Memory Allocator | GPUメモリ確保・サブアロケーション | 同梱ヘッダー表記3.2.0 | MIT |
| ufbx | FBX解析、アニメーションのベイク・キー評価 | v0.23.0 | Alternative A: MIT |
| DirectXMath | ベクトル・行列演算 | Windows SDK同梱版 | MIT |

## D3D12 Memory Allocator

- 著作権者：Advanced Micro Devices, Inc.
- [公式リポジトリ](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)
- 同梱ソース：`Desire/ThirdParty/D3D12MA/`
- ライセンス原本のコピー：[licenses/D3D12MA-LICENSE.txt](licenses/D3D12MA-LICENSE.txt)

```text
Copyright (c) 2019-2026 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## ufbx

- 著作権者：Samuli Raivio
- [公式リポジトリ](https://github.com/ufbx/ufbx)
- [使用版v0.23.0](https://github.com/ufbx/ufbx/tree/v0.23.0)
- 固定コミット：`fcc5d6ba444cfd3eb80677dba5e37e493941abe5`
- 同梱ソース：`Desire/ThirdParty/ufbx/`
- このプロジェクトでは **Alternative A — MIT** を選択します。
- ライセンス原本のコピー：[licenses/ufbx-LICENSE.txt](licenses/ufbx-LICENSE.txt)

原本はMITとPublic Domainの選択肢を含みます。以下も原本の内容を保持しています。

```text
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2020 Samuli Raivio
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
----------------------------------------
```

## DirectXMath

- 著作権者：Microsoft Corporation
- [公式リポジトリ](https://github.com/microsoft/DirectXMath)
- [公式ライセンス](https://github.com/microsoft/DirectXMath/blob/main/LICENSE)
- [Windows SDKへの同梱について](https://learn.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-getting-started)
- ベクトル・行列演算のヘッダーとして使用します。
- 確認環境：Windows SDK 10.0.26100.0のヘッダー、`DIRECTX_MATH_VERSION=319`。
- 実際にビルドへ使われる版は、選択したWindows SDKに依存します。
- ライセンス本文：[licenses/DirectXMath-LICENSE.txt](licenses/DirectXMath-LICENSE.txt)

```text
MIT License

    Copyright (c) Microsoft Corporation.
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE
```

## Microsoft製プラットフォーム基盤

Win32、Direct3D 12、DXGI、Windows Imaging Component、D3DCompiler、Visual C++ランタイムも使用します。
これらのWindows / SDK / ランタイムのコンポーネントは、それぞれの適用条件に従います。
この文書でWindows SDK全体やMicrosoft製ランタイム全体をMITとして扱うものではありません。
DirectXMathについては、ヘッダーのMIT表示を確認して上に個別掲載しています。

ソースまたは実行ファイルを配布する際には、対応するライブラリの著作権表示と許諾文を保持してください。
ライブラリを更新・追加した場合は、その版に合わせて本文も更新します。
