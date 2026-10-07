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

using ClearDSV_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
ClearDSV_t g_original_clear = nullptr;
void* g_clear_target = nullptr;
ID3D11Device* g_trace_device = nullptr;
void (*g_trace_log)(const char*, ...) = nullptr;
std::atomic<int> g_trace_budget{0};
void describeDepth(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*, const D3D11_TEXTURE2D_DESC&,
                   void (*)(const char*, ...), const char*);
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

void STDMETHODCALLTYPE DetourClearDSV(ID3D11DeviceContext* self, ID3D11DepthStencilView* dsv, UINT flags, FLOAT depth,
                                      UINT8 stencil) {
    if (g_trace_budget.load() > 0 && dsv) {
        const size_t n = g_count.load();
        for (size_t i = 0; i < n; ++i) {
            if (g_candidates[i].dsv.Get() == dsv && g_trace_budget.fetch_sub(1) > 0) {
                g_trace_log("  clear #%d flags=%u value=%.3f; contents before:", 12 - g_trace_budget.load(), flags, depth);
                describeDepth(g_trace_device, self, g_candidates[i].texture.Get(), g_candidates[i].desc, g_trace_log, "pre-clear");
                break;
            }
        }
    }
    g_original_clear(self, dsv, flags, depth, stencil);
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
    g_clear_target = vtable[53];
    if (MH_CreateHook(g_clear_target, reinterpret_cast<void*>(&DetourClearDSV), reinterpret_cast<void**>(&g_original_clear)) != MH_OK ||
        MH_EnableHook(g_clear_target) != MH_OK) {
        g_clear_target = nullptr;
    }
    return true;
}

void DepthCapture::remove() {
    if (!g_target) return;
    MH_DisableHook(g_target);
    MH_RemoveHook(g_target);
    if (g_clear_target) {
        MH_DisableHook(g_clear_target);
        MH_RemoveHook(g_clear_target);
        g_clear_target = nullptr;
    }
    g_target = nullptr;
    reset();
}

void DepthCapture::reset() {
    g_count.store(0);
    for (auto& c : g_candidates) c = Candidate{};
}

namespace {

// Copies `texture` to a staging texture and logs its depth range. Stalls the GPU: debug only.
void describeDepth(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
                   const D3D11_TEXTURE2D_DESC& d, void (*log)(const char*, ...), const char* tag) {
    if (d.SampleDesc.Count != 1) return;
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
        log("      staging create failed");
        return;
    }
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
        log("      map failed");
        return;
    }
    if (d.Format != DXGI_FORMAT_D32_FLOAT && d.Format != DXGI_FORMAT_R32_TYPELESS && d.Format != DXGI_FORMAT_R32_FLOAT) {
        log("      format not decoded");
        context->Unmap(staging.Get(), 0);
        return;
    }
    double lo = 1e30, hi = -1e30;
    size_t one = 0, total = 0;
    for (UINT y = 0; y < d.Height; y += 4) {
        const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(m.pData) + y * m.RowPitch);
        for (UINT x = 0; x < d.Width; x += 4) {
            const double v = row[x];
            lo = (std::min)(lo, v);
            hi = (std::max)(hi, v);
            if (v == 1.0) ++one;
            ++total;
        }
    }
    std::string line;
    char buf[48];
    for (int k = 1; k <= 8; ++k) {
        const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(m.pData) + (d.Height * k / 9) * m.RowPitch);
        std::snprintf(buf, sizeof(buf), " %.5f", row[d.Width / 2]);
        line += buf;
    }
    log("      %s min=%.6f max=%.6f ones=%.1f%% centre:%s", tag, lo, hi, 100.0 * one / (std::max<size_t>)(total, 1), line.c_str());
    context->Unmap(staging.Get(), 0);
}

} // namespace

void DepthCapture::dump(ID3D11Device* device, ID3D11DeviceContext* context, void (*log)(const char*, ...)) {
    const size_t n = g_count.load();
    log("depth dump: %zu candidate(s)", n);
    for (size_t i = 0; i < n; ++i) {
        const Candidate& c = g_candidates[i];
        const D3D11_TEXTURE2D_DESC& d = c.desc;
        log("  [%zu] %ux%u fmt=%u samples=%u bind=0x%x binds=%u", i, d.Width, d.Height, static_cast<unsigned>(d.Format),
            d.SampleDesc.Count, d.BindFlags, c.binds);
        describeDepth(device, context, c.texture.Get(), d, log, "now");
    }
}

void DepthCapture::armTrace(ID3D11Device* device, void (*log)(const char*, ...)) {
    g_trace_device = device;
    g_trace_log = log;
    g_trace_budget.store(12);
}
