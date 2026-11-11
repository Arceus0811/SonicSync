#define NOMINMAX
#include "artwork.hpp"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <limits>

namespace sonicsync::windows {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT kArtworkSize = 512;
constexpr std::size_t kMaxEncodedBytes = 8U * 1024U * 1024U;
constexpr UINT kMaxSourceDimension = 20000;
}

ArtworkPtr decodeArtwork(const std::uint8_t* encoded, std::size_t size) noexcept {
    if (!encoded || size == 0 || size > kMaxEncodedBytes || size > std::numeric_limits<DWORD>::max()) return {};
    try {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) return {};
        ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(encoded), static_cast<DWORD>(size)))) return {};
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) return {};
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) return {};
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 ||
            width > kMaxSourceDimension || height > kMaxSourceDimension) return {};

        const UINT side = width < height ? width : height;
        WICRect crop{static_cast<INT>((width - side) / 2), static_cast<INT>((height - side) / 2),
                     static_cast<INT>(side), static_cast<INT>(side)};
        ComPtr<IWICBitmapClipper> clipper;
        if (FAILED(factory->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(frame.Get(), &crop))) return {};
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(clipper.Get(), kArtworkSize, kArtworkSize, WICBitmapInterpolationModeFant))) return {};
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                         nullptr, 0.0, WICBitmapPaletteTypeCustom))) return {};

        auto pixels = std::make_shared<ArtworkPixels>();
        pixels->width = pixels->height = kArtworkSize;
        pixels->bgra.resize(static_cast<std::size_t>(kArtworkSize) * kArtworkSize * 4U);
        if (FAILED(converter->CopyPixels(nullptr, kArtworkSize * 4U,
                                         static_cast<UINT>(pixels->bgra.size()), pixels->bgra.data()))) return {};
        return pixels;
    } catch (...) {
        return {};
    }
}

} // namespace sonicsync::windows
