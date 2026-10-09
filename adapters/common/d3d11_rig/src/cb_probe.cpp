#include "d3d11_rig/cb_probe.hpp"

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

namespace mc::d3d11::cbprobe {
namespace {

constexpr size_t kMapVtableIndex = 14, kUnmapVtableIndex = 15, kUpdateSubresourceVtableIndex = 48;
constexpr float kTolerance = 0.25f;
constexpr UINT kMaxScanBytes = 64 * 1024;

using Map_t = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using Unmap_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using Update_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);

std::atomic<bool> g_enabled{false};
std::atomic<float> g_camera[3], g_player[3];
void (*g_log)(const char*, ...) = nullptr;
std::vector<void*> g_hooked;

struct Mapped {
    ID3D11DeviceContext* context;
    ID3D11Resource* resource;
    void* data;
    UINT bytes;
};
thread_local std::vector<Mapped> t_mapped;

std::mutex g_found_mutex;
std::map<std::tuple<int, int, UINT, UINT>, unsigned> g_found; // set, target (0 camera / 1 player), buffer bytes, offset

// ByteWidth of a constant buffer, 0 for anything else.
UINT constantBufferBytes(ID3D11Resource* resource) {
    if (!resource) return 0;
    D3D11_RESOURCE_DIMENSION dim{};
    resource->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return 0;
    D3D11_BUFFER_DESC desc{};
    static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
    return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) ? desc.ByteWidth : 0;
}

bool near3(const float* p, const float* t) {
    return std::fabs(p[0] - t[0]) < kTolerance && std::fabs(p[1] - t[1]) < kTolerance && std::fabs(p[2] - t[2]) < kTolerance;
}

void scan(int set, const void* data, UINT bytes) {
    if (!data || bytes < 12 || bytes > kMaxScanBytes) return;
    const float cam[3] = {g_camera[0].load(), g_camera[1].load(), g_camera[2].load()};
    const float ply[3] = {g_player[0].load(), g_player[1].load(), g_player[2].load()};
    if (cam[0] == 0.0f && cam[1] == 0.0f && cam[2] == 0.0f) return;
    const auto* f = static_cast<const float*>(data);
    for (UINT off = 0; off + 12 <= bytes; off += 4) {
        for (int which = 0; which < 2; ++which) {
            if (!near3(f + off / 4, which == 0 ? cam : ply)) continue;
            std::lock_guard<std::mutex> lock(g_found_mutex);
            unsigned& count = g_found[{set, which, bytes, off}];
            if (++count == 1 && g_found.size() <= 40 && g_log) {
                g_log("CB probe: %s position found in a %u-byte constant buffer at +%u (context set %d)", which == 0 ? "CAMERA" : "PLAYER", bytes, off, set);
                const UINT from = off >= 64 ? off - 64 : 0, to = std::min<UINT>(bytes, off + 128);
                for (UINT o = from; o + 16 <= to; o += 16) {
                    const float* r = f + o / 4;
                    g_log("  +%03u: %10.4f %10.4f %10.4f %10.4f", o, r[0], r[1], r[2], r[3]);
                }
            }
        }
    }
}

template <int N>
struct HookSet {
    static inline Map_t original_map = nullptr;
    static inline Unmap_t original_unmap = nullptr;
    static inline Update_t original_update = nullptr;

    static HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext* self, ID3D11Resource* res, UINT sub, D3D11_MAP type, UINT flags,
                                         D3D11_MAPPED_SUBRESOURCE* out) {
        const HRESULT hr = original_map(self, res, sub, type, flags, out);
        if (g_enabled.load() && SUCCEEDED(hr) && out && out->pData && type != D3D11_MAP_READ) {
            if (const UINT bytes = constantBufferBytes(res)) {
                if (t_mapped.size() > 256) t_mapped.clear();
                t_mapped.push_back({self, res, out->pData, bytes});
            }
        }
        return hr;
    }
    static void STDMETHODCALLTYPE unmap(ID3D11DeviceContext* self, ID3D11Resource* res, UINT sub) {
        if (g_enabled.load()) {
            for (size_t i = t_mapped.size(); i-- > 0;) {
                if (t_mapped[i].context == self && t_mapped[i].resource == res) {
                    scan(N, t_mapped[i].data, t_mapped[i].bytes); // still mapped: the data the GPU will see
                    t_mapped.erase(t_mapped.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
        }
        original_unmap(self, res, sub);
    }
    static void STDMETHODCALLTYPE update(ID3D11DeviceContext* self, ID3D11Resource* dst, UINT sub, const D3D11_BOX* box, const void* src,
                                         UINT row, UINT depth) {
        if (g_enabled.load() && !box && src) {
            if (const UINT bytes = constantBufferBytes(dst)) scan(N, src, bytes);
        }
        original_update(self, dst, sub, box, src, row, depth);
    }

    static bool install(ID3D11DeviceContext* ctx) {
        void** vtable = *reinterpret_cast<void***>(ctx);
        bool ok = true;
        auto hook = [&](size_t index, void* detour, void** original) {
            void* target = vtable[index];
            if (std::find(g_hooked.begin(), g_hooked.end(), target) != g_hooked.end()) return;
            if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) {
                ok = false;
                return;
            }
            g_hooked.push_back(target);
        };
        hook(kMapVtableIndex, reinterpret_cast<void*>(&map), reinterpret_cast<void**>(&original_map));
        hook(kUnmapVtableIndex, reinterpret_cast<void*>(&unmap), reinterpret_cast<void**>(&original_unmap));
        hook(kUpdateSubresourceVtableIndex, reinterpret_cast<void*>(&update), reinterpret_cast<void**>(&original_update));
        return ok;
    }
};

} // namespace

bool installOn(int set, ID3D11DeviceContext* context) {
    if (!context) return false;
    return set == 0 ? HookSet<0>::install(context) : HookSet<1>::install(context);
}

void setLog(void (*log)(const char*, ...)) { g_log = log; }

void setTargets(const float camera[3], const float player[3]) {
    for (int i = 0; i < 3; ++i) {
        g_camera[i].store(camera[i]);
        g_player[i].store(player[i]);
    }
}

void enable(bool on) { g_enabled.store(on); }

} // namespace mc::d3d11::cbprobe
