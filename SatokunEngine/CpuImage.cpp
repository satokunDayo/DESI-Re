// Windows標準のWICで画像をRGBA8へ展開する。WIC内部の作業メモリは自作Arenaの管理外。
#include "CpuImage.h"
#include "GpuMemory.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <limits>
#include <stdexcept>

// pragmaのcommentはC++の説明コメントではなく、リンク指定。今回はこの指定も変えない。
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// 圧縮されたファイルから、1ピクセルずつ読める画素配列へ変換する入口。
CpuImage CpuImage::Decode(const BinaryAsset& source, MemoryManager& memory, ArenaType scope)
{
    using Microsoft::WRL::ComPtr;
    // WICはWindows SDKの機能。追加の画像ライブラリは導入しない。
    // WICはCOMを使うので、このスレッドのCOM利用を準備する。
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 既に別方式でCOM初期化済みなら、その環境を使う。自分が初期化した分だけ後で解除する。
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) CheckGpu(initialized);
    // 途中で例外が出てもCoUninitializeを忘れないための小さな返却係。
    struct Apartment { bool owned; ~Apartment() { if (owned) CoUninitialize(); } } apartment{ SUCCEEDED(initialized) };
    const auto bytes = source.GetBytes();
    if (bytes.empty() || bytes.size() > (std::numeric_limits<DWORD>::max)())
        throw std::invalid_argument("Image input is empty or too large.");
    // COMのRelease忘れはComPtrへ任せる。Windows SDKの仕組みで、追加ライブラリではない。
    ComPtr<IWICImagingFactory> factory;
    CheckGpu(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    CheckGpu(factory->CreateStream(&stream));
    // WICの入力APIが非constポインタを要求する。デコーダーは読み取り専用で使用。
    CheckGpu(stream->InitializeFromMemory(
        reinterpret_cast<BYTE*>(const_cast<std::byte*>(bytes.data())), static_cast<DWORD>(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    CheckGpu(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    // 最初の1枚だけを読む。アニメGIFなどの全フレーム再生はこの処理にはない。
    CheckGpu(decoder->GetFrame(0, &frame));
    CpuImage image;
    CheckGpu(frame->GetSize(&image.width, &image.height));
    // 掛け算を64ビットで行ってから上限を確認。小さい整数であふれてから検査しても遅い。
    const auto total = std::uint64_t(image.width) * image.height * 4;
    if (!image.width || !image.height || image.width > 16384 || image.height > 16384 || total > 128 * GpuMiB)
        throw std::invalid_argument("Image exceeds the 128 MiB CPU allocation limit.");
    ComPtr<IWICFormatConverter> converter;
    CheckGpu(factory->CreateFormatConverter(&converter));
    // 元が何の画素形式でも、この先はR/G/B/A各8ビットに統一する。
    CheckGpu(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    // 展開先をCPU Arenaから確保。圧縮ファイルより大きくなるので、こちらにも容量が要る。
    image.pixels = memory.Allocate(scope, static_cast<std::size_t>(total), 16);
    if (!image.pixels) throw std::bad_alloc();
    // ここで実際に画素が埋まる。戻った後はWICの一時オブジェクトがなくても画像を使える。
    CheckGpu(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(total),
        static_cast<BYTE*>(image.pixels.Data())));
    return image;
}
