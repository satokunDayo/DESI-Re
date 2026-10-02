// 共有キャッシュと未使用回収の実装。参照数で判断する方式で、循環参照を探すGCではない。
#include "ResourceManager.h"
#include <cmath>
#include <fstream>
#include <stdexcept>

// 相対rootは作成時に絶対パスへ固定する。後でカレントフォルダが変わっても基準がぶれない。
ResourceManager::ResourceManager(MemoryManager& memory, std::filesystem::path root, double delay)
    : memory_(memory), root_(std::filesystem::absolute(root).lexically_normal()), unusedDelay_(delay)
{
    if (!std::isfinite(delay) || delay < 0.0)
        throw std::invalid_argument("Unused delay must be finite and nonnegative.");
}

// 受付確認→目録確認→必要ならファイル読込→共有登録、という順番。
BinaryAssetHandle ResourceManager::AcquireBinary(const std::filesystem::path& relativePath,
    ArenaType scope)
{
    // 空や絶対パスは拒否。ただし..を使ったroot外参照を封じる安全境界までは実装していない。
    if (relativePath.empty() || relativePath.has_root_path())
        throw std::invalid_argument("An asset path must be relative to the asset root.");
    auto arena = memory_.GetArena(scope);
    // ファイルを開く前から作業札を持つ。CPUバッファがまだなくてもArenaの閉店完了を止められる。
    auto operation = arena->BeginOperation();
    if (!operation) throw std::runtime_error("The asset arena is closing.");
    const Key key{arena->GetId(), (root_ / relativePath).lexically_normal()};

    std::shared_ptr<Entry> entry;
    // promiseは結果を渡す側、futureは待つ側。同じファイルの二重読込をまとめる。
    std::shared_ptr<std::promise<BinaryAssetHandle>> producer;
    std::shared_future<BinaryAssetHandle> pending;
    {
        std::lock_guard lock(mutex_);
        if (auto found = entries_.find(key); found != entries_.end())
        {
            entry = found->second;
            // 完成品があればその場で共有して返す。使われ始めたので未使用時計を取り消す。
            if (entry->asset)
            {
                entry->unusedSince.reset();
                return entry->asset;
            }
            pending = entry->pending;
        }
        else
        {
            // 最初の呼び出しだけが読み手になる。次の呼び出しは同じfutureを待つ。
            producer = std::make_shared<std::promise<BinaryAssetHandle>>();
            entry = std::make_shared<Entry>();
            entry->scope = scope;
            entry->pending = producer->get_future().share();
            entries_.emplace(key, entry);
        }
    }

    // A second caller waits on the existing read, without holding any lock.
    // ロックを持ったまま待つと、読み手が結果を登録できない。ここはロックの外。
    if (!producer) return pending.get();
    try
    {
        // 実ファイルのI/Oも目録ロックの外。Managerが裏でスレッドを生やすわけではない。
        auto asset = ReadFile(arena, key.second);
        {
            std::lock_guard lock(mutex_);
            const auto found = entries_.find(key);
            // 読込中にシーン終了したなら、出来上がったデータを古い目録へ復活させない。
            if (found == entries_.end() || found->second != entry || !arena->IsOpen())
                throw std::runtime_error("Asset loading was cancelled by arena closure.");
            entry->asset = asset;
            entry->pending = {};
        }
        producer->set_value(asset);
        return asset;
    }
    catch (...)
    {
        const auto error = std::current_exception();
        {
            std::lock_guard lock(mutex_);
            const auto found = entries_.find(key);
            if (found != entries_.end() && found->second == entry) entries_.erase(found);
        }
        // 'entry' keeps any payload alive until after the map lock is released.
        // 待っている側にも同じ失敗を知らせる。無期限に結果待ちさせない。
        producer->set_exception(error);
        std::rethrow_exception(error);
    }
}

// ファイルサイズを先に調べ、その分のMemoryBlockへ読み込む。空ファイルも有効。
BinaryAssetHandle ResourceManager::ReadFile(const std::shared_ptr<MemoryArena>& arena,
    const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open asset: " + path.string());
    const auto length = file.tellg();
    if (length < std::streampos{0}) throw std::runtime_error("Cannot read file size.");
    if (length > std::streampos{static_cast<std::streamoff>(BlockAllocator::MaxAllocationSize)})
        throw std::length_error("Asset exceeds the 128 MiB per-allocation limit.");
    const auto bytes = static_cast<std::size_t>(static_cast<std::streamoff>(length));
    MemoryBlock block;
    if (bytes != 0)
    {
        block = arena->Allocate(bytes);
        if (!block)
        {
            // 確保失敗なら未使用分を回収し、1度だけ再挑戦。使用中のHandleからは奪わない。
            CollectUnusedNow(arena->GetId());
            block = arena->Allocate(bytes);
        }
        if (!block) throw std::bad_alloc();
    }
    file.seekg(0, std::ios::beg);
    if (bytes != 0) file.read(static_cast<char*>(block.Data()), static_cast<std::streamsize>(bytes));
    if (!file) throw std::runtime_error("Asset file read failed.");
    return BinaryAssetHandle(new BinaryAsset(std::move(block)));
}

// use_count()==1なら、このキャッシュだけが所有している。外の利用者がいない目印。
void ResourceManager::CollectUnusedNow(std::uint64_t arenaId)
{
    std::vector<std::shared_ptr<Entry>> retired;
    {
        std::lock_guard lock(mutex_);
        retired.reserve(entries_.size());
        for (auto it = entries_.begin(); it != entries_.end();)
        {
            const auto& entry = *it->second;
            if (it->first.first == arenaId && entry.asset && entry.asset.use_count() == 1)
            {
                retired.push_back(it->second);
                it = entries_.erase(it);
            }
            else ++it;
        }
    }
    // RAII returns payloads here, outside the cache lock.
}

// 未使用を最初に観測した時刻から猶予を数える。最後の利用終了時刻を直接通知しているわけではない。
void ResourceManager::Update(double delta)
{
    std::vector<std::shared_ptr<Entry>> retired;
    {
        std::lock_guard lock(mutex_);
        if (!std::isfinite(delta) || delta < 0.0 || !std::isfinite(now_ + delta))
            throw std::invalid_argument("Elapsed time must be finite and nonnegative.");
        now_ += delta;
        retired.reserve(entries_.size());
        for (auto it = entries_.begin(); it != entries_.end();)
        {
            auto& entry = *it->second;
            // 読込中、または他に所有者がいるものは回収対象から外す。
            if (!entry.asset || entry.asset.use_count() > 1)
            {
                entry.unusedSince.reset();
                ++it;
                continue;
            }
            if (!entry.unusedSince) entry.unusedSince = now_;
            if (now_ - *entry.unusedSince >= unusedDelay_)
            {
                retired.push_back(it->second);
                it = entries_.erase(it);
            }
            else ++it;
        }
    }
}

// 対象の目録を外し、実際の破棄はretiredがスコープを抜けるとき。ロック外でArenaへ返す。
void ResourceManager::DropCache(std::optional<ArenaType> scope, std::optional<std::uint64_t> arenaId)
{
    std::vector<std::shared_ptr<Entry>> retired;
    {
        std::lock_guard lock(mutex_);
        retired.reserve(entries_.size());
        for (auto it = entries_.begin(); it != entries_.end();)
        {
            if ((!scope || it->second->scope == *scope) && (!arenaId || it->first.first == *arenaId))
            {
                retired.push_back(it->second);
                it = entries_.erase(it);
            }
            else ++it;
        }
    }
}

// 先にArenaの受付を止め、そのIDのSceneキャッシュだけ外す。Systemの資産は残す。
void ResourceManager::BeginSceneClose()
{
    const auto closingId = memory_.BeginSceneClose();
    DropCache(ArenaType::Scene, closingId);
}
// アプリ終了時は両方のArenaを閉め、全キャッシュの所有を外す。
void ResourceManager::BeginShutdown()
{
    memory_.BeginShutdown();
    DropCache(std::nullopt);
}
// 読み込みが完了し、目録に残っている件数。GPU画像数ではない。
std::size_t ResourceManager::GetResidentCount() const
{
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& [key, entry] : entries_) if (entry->asset) ++count;
    return count;
}
// 目録にある未完了件数。実行中スレッド数とは限らない。
std::size_t ResourceManager::GetPendingCount() const
{
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& [key, entry] : entries_) if (!entry->asset) ++count;
    return count;
}
// 名前に注意。2つのArenaの予約量合計なので、ファイル以外のPool等も含まれる。
std::size_t ResourceManager::GetUsedBytes() const
{
    return memory_.GetArena(ArenaType::System)->GetSnapshot().blocks.reservedBytes
        + memory_.GetArena(ArenaType::Scene)->GetSnapshot().blocks.reservedBytes;
}
