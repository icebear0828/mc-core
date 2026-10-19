#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include <vector>

#include "png_wic.hpp"

namespace erov {
namespace {

template <typename T>
struct Com {
    T* p{nullptr};
    ~Com() {
        if (p) p->Release();
    }
    T** put() { return &p; }
    T* operator->() const { return p; }
};

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

} // namespace

bool DecodePngFile(const std::string& path, std::vector<uint8_t>& rgba, unsigned& width, unsigned& height) {
    rgba.clear();
    width = height = 0;
    // The thread may already be in a COM apartment (RPC_E_CHANGED_MODE): that is fine for WIC, only a real failure matters.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(init);
    bool ok = false;
    {
        Com<IWICImagingFactory> factory;
        Com<IWICBitmapDecoder> decoder;
        Com<IWICBitmapFrameDecode> frame;
        Com<IWICFormatConverter> converter;
        UINT w = 0, h = 0;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))) &&
            SUCCEEDED(factory->CreateDecoderFromFilename(Widen(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) &&
            SUCCEEDED(decoder->GetFrame(0, frame.put())) && SUCCEEDED(factory->CreateFormatConverter(converter.put())) &&
            SUCCEEDED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0f,
                                            WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(converter->GetSize(&w, &h)) && w != 0 && h != 0 && w <= 8192 && h <= 8192) {
            rgba.resize(static_cast<size_t>(w) * h * 4);
            if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(rgba.size()), rgba.data()))) {
                width = w;
                height = h;
                ok = true;
            } else {
                rgba.clear();
            }
        }
    }
    if (uninit) CoUninitialize();
    return ok;
}

bool EncodePngFile(const std::string& path, const uint8_t* rgba, unsigned width, unsigned height, unsigned pitch) {
    if (rgba == nullptr || width == 0 || height == 0 || pitch < width * 4) return false;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(init);
    bool ok = false;
    // WIC's PNG encoder expects 32bppBGRA; feeding it 32bppRGBA makes
    // SetPixelFormat fail and leaves a 0-byte file.
    std::vector<uint8_t> bgra(static_cast<size_t>(pitch) * height);
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        bgra[i + 0] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i + 0];
        bgra[i + 3] = rgba[i + 3];
    }
    {
        Com<IWICImagingFactory> factory;
        Com<IWICStream> stream;
        Com<IWICBitmapEncoder> encoder;
        Com<IWICBitmapFrameEncode> frame;
        const std::wstring wide = Widen(path);
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))) &&
            SUCCEEDED(factory->CreateStream(stream.put())) && SUCCEEDED(stream->InitializeFromFilename(wide.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) &&
            SUCCEEDED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(frame.put(), nullptr)) &&
            SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height))) {
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
                SUCCEEDED(frame->WritePixels(height, pitch, pitch * height, bgra.data())) && SUCCEEDED(frame->Commit()) &&
                SUCCEEDED(encoder->Commit())) {
                ok = true;
            }
        }
    }
    if (uninit) CoUninitialize();
    return ok;
}

} // namespace erov
