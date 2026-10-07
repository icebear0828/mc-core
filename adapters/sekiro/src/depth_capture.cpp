#include "depth_capture.hpp"

#include <MinHook.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sekiro::render {

namespace {

using Microsoft::WRL::ComPtr;
using OMSetRenderTargets_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
                                                      ID3D11DepthStencilView*);
constexpr size_t kOMSetRenderTargetsVtableIndex = 33;
constexpr size_t kMaxCandidates = 16;

struct Candidate {
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc{};
    uint32_t binds{0};
    uint32_t binds_with_color{0};
};

OMSetRenderTargets_t g_original = nullptr;
void* g_target = nullptr;
std::array<Candidate, kMaxCandidates> g_candidates;
std::atomic<size_t> g_count{0};

void STDMETHODCALLTYPE DetourOMSetRenderTargets(ID3D11DeviceContext* self, UINT num, ID3D11RenderTargetView* const* rtvs,
                                                ID3D11DepthStencilView* dsv) {
    if (dsv && self->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        const size_t n = g_count.load();
        size_t found = n;
        for (size_t i = 0; i < n; ++i) {
            if (g_candidates[i].dsv.Get() == dsv) {
                found = i;
                break;
            }
        }
        if (found == n && n < kMaxCandidates) {
            ComPtr<ID3D11Resource> res;
            dsv->GetResource(res.GetAddressOf());
            ComPtr<ID3D11Texture2D> tex;
            if (res && SUCCEEDED(res.As(&tex))) {
                g_candidates[n].dsv = dsv;
                g_candidates[n].texture = tex;
                tex->GetDesc(&g_candidates[n].desc);
                g_count.store(n + 1);
            } else {
                found = kMaxCandidates; // not a 2D texture, ignore
            }
        }
        if (found < g_count.load()) {
            ++g_candidates[found].binds;
            if (num > 0 && rtvs && rtvs[0]) ++g_candidates[found].binds_with_color;
        }
    }
    g_original(self, num, rtvs, dsv);
}

} // namespace

bool DepthCapture::install(ID3D11DeviceContext* immediate) {
    if (g_target || !immediate) return g_target != nullptr;
    void** vtable = *reinterpret_cast<void***>(immediate);
    g_target = vtable[kOMSetRenderTargetsVtableIndex];
    if (MH_CreateHook(g_target, reinterpret_cast<void*>(&DetourOMSetRenderTargets), reinterpret_cast<void**>(&g_original)) != MH_OK ||
        MH_EnableHook(g_target) != MH_OK) {
        g_target = nullptr;
        return false;
    }
    return true;
}

void DepthCapture::remove() {
    if (!g_target) return;
    MH_DisableHook(g_target);
    MH_RemoveHook(g_target);
    g_target = nullptr;
    reset();
}

void DepthCapture::reset() {
    g_count.store(0);
    for (auto& c : g_candidates) c = Candidate{};
}

void DepthCapture::dump(ID3D11Device* device, ID3D11DeviceContext* context, void (*log)(const char*, ...)) {
    const size_t n = g_count.load();
    log("depth dump: %zu candidate(s)", n);
    for (size_t i = 0; i < n; ++i) {
        const Candidate& c = g_candidates[i];
        const D3D11_TEXTURE2D_DESC& d = c.desc;
        log("  [%zu] %ux%u fmt=%u samples=%u bind=0x%x mips=%u binds=%u with_color=%u", i, d.Width, d.Height,
            static_cast<unsigned>(d.Format), d.SampleDesc.Count, d.BindFlags, d.MipLevels, c.binds, c.binds_with_color);
        if (d.SampleDesc.Count != 1) continue;

        D3D11_TEXTURE2D_DESC sd = d;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
            log("      staging create failed");
            continue;
        }
        context->CopyResource(staging.Get(), c.texture.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
            log("      map failed");
            continue;
        }
        // Only 32-bit-per-texel layouts are decoded: R32 (D32 typeless/float) and R24G8.
        const bool is32 = d.Format == DXGI_FORMAT_R32_TYPELESS || d.Format == DXGI_FORMAT_D32_FLOAT ||
                          d.Format == DXGI_FORMAT_R32_FLOAT;
        const bool is24 = d.Format == DXGI_FORMAT_R24G8_TYPELESS || d.Format == DXGI_FORMAT_D24_UNORM_S8_UINT;
        if (is32 || is24) {
            double lo = 1e30, hi = -1e30;
            size_t zero = 0, one = 0, total = 0;
            for (UINT y = 0; y < d.Height; y += 4) {
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m.pData) + y * m.RowPitch);
                for (UINT x = 0; x < d.Width; x += 4) {
                    double v;
                    if (is32) {
                        float f;
                        std::memcpy(&f, &row[x], 4);
                        v = f;
                    } else {
                        v = (row[x] & 0xFFFFFF) / 16777215.0;
                    }
                    lo = (std::min)(lo, v);
                    hi = (std::max)(hi, v);
                    if (v == 0.0) ++zero;
                    if (v == 1.0) ++one;
                    ++total;
                }
            }
            log("      depth min=%.6f max=%.6f zeros=%.1f%% ones=%.1f%%", lo, hi, 100.0 * zero / (std::max<size_t>)(total, 1),
                100.0 * one / (std::max<size_t>)(total, 1));
            // Centre column, to compare against what is visible there.
            std::string line;
            char buf[48];
            for (int k = 1; k <= 8; ++k) {
                const UINT y = d.Height * k / 9;
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m.pData) + y * m.RowPitch);
                float f;
                if (is32) std::memcpy(&f, &row[d.Width / 2], 4);
                else f = static_cast<float>((row[d.Width / 2] & 0xFFFFFF) / 16777215.0);
                std::snprintf(buf, sizeof(buf), " %.5f", f);
                line += buf;
            }
            log("      centre column (top->bottom):%s", line.c_str());
        } else {
            log("      format not decoded");
        }
        context->Unmap(staging.Get(), 0);
    }
}

} // namespace sekiro::render
