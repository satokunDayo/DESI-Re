#pragma once
// メモリの仕組みを単体で眺めるためのデモ入口。普段のゲーム進行には不要。
// ここでは中身が要らないので前方宣言だけ。実装側で必要なヘッダを読む。
class MemoryManager;
class ResourceManager;
// 渡されたManagerを使って確保・共有・シーン終了を試す。現在のデータを持つ最中には呼ばない。
void RunMemorySystemDemo(MemoryManager& memory, ResourceManager& resources);
