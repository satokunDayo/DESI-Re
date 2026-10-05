// 外部ライブラリの実装を1回だけコンパイルする入口。ufbx.cをVSへ重複登録しない。
// ThirdPartyの原文は変更しない。選択したライセンスはMIT、全文も同じフォルダに置く。
#pragma warning(push, 0)
#include "ThirdParty/ufbx/ufbx.c"
#pragma warning(pop)
