#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

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

} // namespace erov
