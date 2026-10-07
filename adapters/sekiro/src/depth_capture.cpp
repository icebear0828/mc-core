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
using OMSetRTUAV_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*,
                                              UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
using ClearDSV_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
using CreateDeferred_t = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, UINT, ID3D11DeviceContext**);

constexpr size_t kOMSetRenderTargetsVtableIndex = 33;
constexpr size_t kOMSetRTUAVVtableIndex = 34;
constexpr size_t kClearDSVVtableIndex = 53;
constexpr size_t kCreateDeferredContextVtableIndex = 27;
constexpr size_t kMaxCandidates = 16;

struct Candidate {
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc{};
    uint32_t binds{0};
    uint32_t binds_with_color{0};
    D3D11_DEVICE_CONTEXT_TYPE context_type{D3D11_DEVICE_CONTEXT_IMMEDIATE};
};

std::array<Candidate, kMaxCandidates> g_candidates;
std::atomic<size_t> g_count{0};
ID3D11Device* g_trace_device = nullptr;
void (*g_trace_log)(const char*, ...) = nullptr;
std::atomic<int> g_trace_budget{0};
std::atomic<uint64_t> g_calls[2][3]; // [set][OM, OM+UAV, Clear]
std::atomic<uint64_t> g_deferred_created{0};
CreateDeferred_t g_original_create_deferred = nullptr;
void* g_create_deferred_target = nullptr;
std::vector<void*> g_hooked_targets;

void describeDepth(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*, const D3D11_TEXTURE2D_DESC&,
                   void (*)(const char*, ...), const char*);

void record(ID3D11DeviceContext* self, UINT num, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv) {
    if (!dsv) return;
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
            g_candidates[n].context_type = self->GetType();
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

// One set of detours per distinct context implementation (immediate and deferred contexts have their own
// vtables); each set needs its own trampolines.
template <int N>
struct HookSet {
    static inline OMSetRenderTargets_t original_om = nullptr;
    static inline OMSetRTUAV_t original_uav = nullptr;
    static inline ClearDSV_t original_clear = nullptr;

    static void STDMETHODCALLTYPE om(ID3D11DeviceContext* self, UINT num, ID3D11RenderTargetView* const* rtvs,
                                     ID3D11DepthStencilView* dsv) {
        ++g_calls[N][0];
        record(self, num, rtvs, dsv);
        original_om(self, num, rtvs, dsv);
    }
    static void STDMETHODCALLTYPE om_uav(ID3D11DeviceContext* self, UINT num, ID3D11RenderTargetView* const* rtvs,
                                         ID3D11DepthStencilView* dsv, UINT uav_start, UINT num_uavs,
                                         ID3D11UnorderedAccessView* const* uavs, const UINT* counts) {
        ++g_calls[N][1];
        if (num != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) record(self, num, rtvs, dsv);
        original_uav(self, num, rtvs, dsv, uav_start, num_uavs, uavs, counts);
    }
    static void STDMETHODCALLTYPE clear(ID3D11DeviceContext* self, ID3D11DepthStencilView* dsv, UINT flags, FLOAT depth,
                                        UINT8 stencil) {
        ++g_calls[N][2];
        if (g_trace_budget.load() > 0 && dsv) {
            const size_t n = g_count.load();
            for (size_t i = 0; i < n; ++i) {
                if (g_candidates[i].dsv.Get() == dsv && g_trace_budget.fetch_sub(1) > 0) {
                    g_trace_log("  clear #%d (set %d) flags=%u value=%.3f; contents before:", 12 - g_trace_budget.load(), N,
                                flags, depth);
                    describeDepth(g_trace_device, self, g_candidates[i].texture.Get(), g_candidates[i].desc, g_trace_log, "pre-clear");
                    break;
                }
            }
        }
        original_clear(self, dsv, flags, depth, stencil);
    }

    static bool install(ID3D11DeviceContext* ctx) {
        void** vtable = *reinterpret_cast<void***>(ctx);
        bool ok = true;
        auto hook = [&](size_t index, void* detour, void** original) {
            void* target = vtable[index];
            if (std::find(g_hooked_targets.begin(), g_hooked_targets.end(), target) != g_hooked_targets.end()) return;
            if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) {
                ok = false;
                return;
            }
            g_hooked_targets.push_back(target);
        };
        hook(kOMSetRenderTargetsVtableIndex, reinterpret_cast<void*>(&om), reinterpret_cast<void**>(&original_om));
        hook(kOMSetRTUAVVtableIndex, reinterpret_cast<void*>(&om_uav), reinterpret_cast<void**>(&original_uav));
        hook(kClearDSVVtableIndex, reinterpret_cast<void*>(&clear), reinterpret_cast<void**>(&original_clear));
        return ok;
    }
};

bool g_deferred_set_installed = false;

HRESULT STDMETHODCALLTYPE DetourCreateDeferred(ID3D11Device* self, UINT flags, ID3D11DeviceContext** out) {
    const HRESULT hr = g_original_create_deferred(self, flags, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_deferred_created;
        if (!g_deferred_set_installed) {
            g_deferred_set_installed = true;
            HookSet<1>::install(*out);
        }
    }
    return hr;
}

} // namespace

bool DepthCapture::install(ID3D11Device* device, ID3D11DeviceContext* immediate) {
    if (!g_hooked_targets.empty() || !immediate || !device) return !g_hooked_targets.empty();
    bool ok = HookSet<0>::install(immediate);
    void** dvt = *reinterpret_cast<void***>(device);
    g_create_deferred_target = dvt[kCreateDeferredContextVtableIndex];
    if (MH_CreateHook(g_create_deferred_target, reinterpret_cast<void*>(&DetourCreateDeferred),
                      reinterpret_cast<void**>(&g_original_create_deferred)) == MH_OK) {
        MH_EnableHook(g_create_deferred_target);
        g_hooked_targets.push_back(g_create_deferred_target);
    }
    return ok;
}

void DepthCapture::remove() {
    for (void* t : g_hooked_targets) {
        MH_DisableHook(t);
        MH_RemoveHook(t);
    }
    g_hooked_targets.clear();
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
    log("depth dump: %zu candidate(s); immediate calls OM=%llu OM+UAV=%llu Clear=%llu; deferred contexts created=%llu, calls OM=%llu OM+UAV=%llu Clear=%llu",
        n, static_cast<unsigned long long>(g_calls[0][0].load()), static_cast<unsigned long long>(g_calls[0][1].load()),
        static_cast<unsigned long long>(g_calls[0][2].load()), static_cast<unsigned long long>(g_deferred_created.load()),
        static_cast<unsigned long long>(g_calls[1][0].load()), static_cast<unsigned long long>(g_calls[1][1].load()),
        static_cast<unsigned long long>(g_calls[1][2].load()));
    for (size_t i = 0; i < n; ++i) {
        const Candidate& c = g_candidates[i];
        const D3D11_TEXTURE2D_DESC& d = c.desc;
        log("  [%zu] %ux%u fmt=%u samples=%u bind=0x%x binds=%u ctx=%d", i, d.Width, d.Height, static_cast<unsigned>(d.Format),
            d.SampleDesc.Count, d.BindFlags, c.binds, static_cast<int>(c.context_type));
        describeDepth(device, context, c.texture.Get(), d, log, "now");
    }
}

void DepthCapture::armTrace(ID3D11Device* device, void (*log)(const char*, ...)) {
    g_trace_device = device;
    g_trace_log = log;
    g_trace_budget.store(12);
}

} // namespace sekiro::render
