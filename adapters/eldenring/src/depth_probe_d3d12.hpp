#pragma once

// Read-only probe of how the game uses its depth textures within a frame (mc_er_depthprobe.txt switches it on). The overlay tests the figures against one of
// the game's depth textures at Present, and the creatures never hide anything: the reverser says the creatures are drawn after the depth we sample, but the
// claims disagree with each other, so this records what really happens: for three frames, every bind of a depth texture as the DSV, the draws made while it
// is bound, the clears, the copies and the resource-state transitions that involve a depth texture, per command list, and the order the lists are submitted in.
//
// Included by overlay_d3d12.cpp only (Windows). Nothing here changes what the game draws: every detour calls the original first or right after logging.

#include <d3d12.h>

#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "eldenring_d3dstate.hpp"

namespace erov::probe {

using Logger = void (*)(const char* fmt, ...);

struct DsvInfo {
    unsigned long long width{0};
    unsigned height{0};
    int format{0};
    unsigned array_size{0};
};

enum class Kind : uint8_t { Bind, Draws, Clear, Barrier, Copy, Exec };

struct Event {
    Kind kind{Kind::Bind};
    const void* list{nullptr};
    const void* res{nullptr};    // the depth texture (Copy: the destination)
    const void* other{nullptr};  // Copy: the source; Exec: unused
    uint32_t a{0}, b{0};         // Bind: render target count / Draws: how many / Clear: flags, stencil / Barrier: state before, after
    float f{0.f};                // Clear: the depth value
};

struct State {
    std::mutex mutex;
    std::unordered_map<SIZE_T, const void*> dsv_handle;          // DSV descriptor -> resource, updated by every CreateDepthStencilView
    std::unordered_map<const void*, DsvInfo> depth_textures;      // every resource that was ever a DSV (the description is copied: no pointer is dereferenced later)
    std::unordered_map<const void*, const void*> bound;           // command list -> the depth texture bound now
    std::vector<Event> events;
    std::vector<const void*> exec_order;                           // lists in the order the queue received them while a capture frame was open
    std::atomic<bool> active{false};
    int captures{0};
    uint64_t start_ms{0}, next_ms{20000};
    Logger log{nullptr};
};

inline State& S() {
    static State s;
    return s;
}

// ---- originals -------------------------------------------------------------------------------------------------------------------------------------------

using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
using ClearDsvFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8, UINT, const D3D12_RECT*);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
using ResourceBarrierFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
using CopyResourceFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
using CopyTextureRegionFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*);

inline OMSetRenderTargetsFn g_om_orig = nullptr;
inline ClearDsvFn g_clear_orig = nullptr;
inline DrawInstancedFn g_draw_orig = nullptr;
inline DrawIndexedFn g_drawidx_orig = nullptr;
inline ResourceBarrierFn g_barrier_orig = nullptr;
inline CopyResourceFn g_copyres_orig = nullptr;
inline CopyTextureRegionFn g_copytex_orig = nullptr;

// ---- recording -------------------------------------------------------------------------------------------------------------------------------------------

inline bool IsDepthTexture(State& s, const void* res) { return s.depth_textures.find(res) != s.depth_textures.end(); }

inline void AddDraws(const void* list, uint32_t n) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.mutex);
    const auto it = s.bound.find(list);
    if (it == s.bound.end() || it->second == nullptr) return;
    if (!s.events.empty()) { // the draws made while the same texture stays bound are one entry
        Event& last = s.events.back();
        if (last.kind == Kind::Draws && last.list == list && last.res == it->second) {
            last.a += n;
            return;
        }
    }
    Event e;
    e.kind = Kind::Draws;
    e.list = list;
    e.res = it->second;
    e.a = n;
    if (s.events.size() < 20000) s.events.push_back(e);
}

inline void STDMETHODCALLTYPE OMSetRenderTargetsDetour(ID3D12GraphicsCommandList* l, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, BOOL single, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    if (S().active.load(std::memory_order_relaxed)) {
        State& s = S();
        std::lock_guard<std::mutex> g(s.mutex);
        const void* res = nullptr;
        if (dsv != nullptr) {
            const auto it = s.dsv_handle.find(dsv->ptr);
            if (it != s.dsv_handle.end()) res = it->second;
        }
        s.bound[l] = res;
        if (res != nullptr && s.events.size() < 20000) {
            Event e;
            e.kind = Kind::Bind;
            e.list = l;
            e.res = res;
            e.a = n;
            s.events.push_back(e);
        }
    }
    g_om_orig(l, n, rtvs, single, dsv);
}

inline void STDMETHODCALLTYPE ClearDsvDetour(ID3D12GraphicsCommandList* l, D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT n, const D3D12_RECT* rects) {
    if (S().active.load(std::memory_order_relaxed)) {
        State& s = S();
        std::lock_guard<std::mutex> g(s.mutex);
        const auto it = s.dsv_handle.find(dsv.ptr);
        if (it != s.dsv_handle.end() && s.events.size() < 20000) {
            Event e;
            e.kind = Kind::Clear;
            e.list = l;
            e.res = it->second;
            e.a = static_cast<uint32_t>(flags);
            e.b = stencil;
            e.f = depth;
            s.events.push_back(e);
        }
    }
    g_clear_orig(l, dsv, flags, depth, stencil, n, rects);
}

inline void STDMETHODCALLTYPE DrawInstancedDetour(ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, UINT d) {
    if (S().active.load(std::memory_order_relaxed)) AddDraws(l, 1);
    g_draw_orig(l, a, b, c, d);
}

inline void STDMETHODCALLTYPE DrawIndexedDetour(ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, INT d, UINT e) {
    if (S().active.load(std::memory_order_relaxed)) AddDraws(l, 1);
    g_drawidx_orig(l, a, b, c, d, e);
}

inline void STDMETHODCALLTYPE ResourceBarrierDetour(ID3D12GraphicsCommandList* l, UINT n, const D3D12_RESOURCE_BARRIER* b) {
    if (S().active.load(std::memory_order_relaxed) && b != nullptr) {
        State& s = S();
        std::lock_guard<std::mutex> g(s.mutex);
        for (UINT i = 0; i < n; ++i) {
            if (b[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) continue;
            const void* res = b[i].Transition.pResource;
            if (!IsDepthTexture(s, res) || s.events.size() >= 20000) continue;
            Event e;
            e.kind = Kind::Barrier;
            e.list = l;
            e.res = res;
            e.a = static_cast<uint32_t>(b[i].Transition.StateBefore);
            e.b = static_cast<uint32_t>(b[i].Transition.StateAfter);
            s.events.push_back(e);
        }
    }
    g_barrier_orig(l, n, b);
}

inline void NoteCopy(const void* list, const void* dst, const void* src) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.mutex);
    if ((!IsDepthTexture(s, dst) && !IsDepthTexture(s, src)) || s.events.size() >= 20000) return;
    Event e;
    e.kind = Kind::Copy;
    e.list = list;
    e.res = dst;
    e.other = src;
    s.events.push_back(e);
}

inline void STDMETHODCALLTYPE CopyResourceDetour(ID3D12GraphicsCommandList* l, ID3D12Resource* dst, ID3D12Resource* src) {
    if (S().active.load(std::memory_order_relaxed)) NoteCopy(l, dst, src);
    g_copyres_orig(l, dst, src);
}

inline void STDMETHODCALLTYPE CopyTextureRegionDetour(ID3D12GraphicsCommandList* l, const D3D12_TEXTURE_COPY_LOCATION* dst, UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* src, const D3D12_BOX* box) {
    if (S().active.load(std::memory_order_relaxed) && dst != nullptr && src != nullptr) NoteCopy(l, dst->pResource, src->pResource);
    g_copytex_orig(l, dst, x, y, z, src, box);
}

// ---- hooks entry points ----------------------------------------------------------------------------------------------------------------------------------

// Every CreateDepthStencilView of the game, whatever its size or format.
inline void NoteDsv(SIZE_T handle, ID3D12Resource* res) {
    if (res == nullptr) return;
    const D3D12_RESOURCE_DESC rd = res->GetDesc();
    State& s = S();
    std::lock_guard<std::mutex> g(s.mutex);
    s.dsv_handle[handle] = res;
    s.depth_textures[res] = DsvInfo{rd.Width, rd.Height, static_cast<int>(rd.Format), rd.DepthOrArraySize};
}

// Every submission the game makes while a capture frame is open.
inline void NoteExecute(UINT n, ID3D12CommandList* const* lists) {
    if (!S().active.load(std::memory_order_relaxed)) return;
    State& s = S();
    std::lock_guard<std::mutex> g(s.mutex);
    for (UINT i = 0; i < n; ++i) {
        s.exec_order.push_back(lists[i]);
        Event e;
        e.kind = Kind::Exec;
        e.list = lists[i];
        if (s.events.size() < 20000) s.events.push_back(e);
    }
}

inline bool Install(ID3D12GraphicsCommandList* any_list, Logger log) {
    State& s = S();
    s.log = log;
    if (any_list == nullptr) return false;
    void** vt = *reinterpret_cast<void***>(any_list);
    struct Target { size_t index; void* detour; void** orig; const char* name; };
    const Target targets[] = {
        {12, reinterpret_cast<void*>(&DrawInstancedDetour), reinterpret_cast<void**>(&g_draw_orig), "DrawInstanced"},
        {13, reinterpret_cast<void*>(&DrawIndexedDetour), reinterpret_cast<void**>(&g_drawidx_orig), "DrawIndexedInstanced"},
        {16, reinterpret_cast<void*>(&CopyTextureRegionDetour), reinterpret_cast<void**>(&g_copytex_orig), "CopyTextureRegion"},
        {17, reinterpret_cast<void*>(&CopyResourceDetour), reinterpret_cast<void**>(&g_copyres_orig), "CopyResource"},
        {26, reinterpret_cast<void*>(&ResourceBarrierDetour), reinterpret_cast<void**>(&g_barrier_orig), "ResourceBarrier"},
        {46, reinterpret_cast<void*>(&OMSetRenderTargetsDetour), reinterpret_cast<void**>(&g_om_orig), "OMSetRenderTargets"},
        {47, reinterpret_cast<void*>(&ClearDsvDetour), reinterpret_cast<void**>(&g_clear_orig), "ClearDepthStencilView"},
    };
    for (const Target& t : targets) {
        if (MH_CreateHook(vt[t.index], t.detour, t.orig) != MH_OK || MH_EnableHook(vt[t.index]) != MH_OK) {
            if (log) log("depthprobe: hooking %s failed", t.name);
            return false;
        }
    }
    s.start_ms = GetTickCount64();
    if (log) log("depthprobe: hooked the command list methods; three frames are recorded (at 20 s, then every 12 s after the previous one)");
    return true;
}

// ---- reporting -------------------------------------------------------------------------------------------------------------------------------------------

inline void Dump() {
    State& s = S();
    std::vector<Event> events;
    std::vector<const void*> exec;
    std::unordered_map<const void*, DsvInfo> textures;
    {
        std::lock_guard<std::mutex> g(s.mutex);
        events = s.events;
        exec = s.exec_order;
        textures = s.depth_textures;
        s.events.clear();
        s.exec_order.clear();
        s.bound.clear();
    }
    if (!s.log) return;
    // short names for the textures in the order they first appear
    std::vector<const void*> tex_order;
    auto tex_label = [&](const void* r) -> std::string {
        for (size_t i = 0; i < tex_order.size(); ++i) {
            if (tex_order[i] == r) return std::string(1, static_cast<char>('A' + i % 26)) + (i >= 26 ? std::to_string(i / 26) : "");
        }
        tex_order.push_back(r);
        const size_t i = tex_order.size() - 1;
        return std::string(1, static_cast<char>('A' + i % 26)) + (i >= 26 ? std::to_string(i / 26) : "");
    };
    std::vector<const void*> list_order;
    auto list_label = [&](const void* l) -> std::string {
        for (size_t i = 0; i < list_order.size(); ++i) if (list_order[i] == l) return "L" + std::to_string(i);
        list_order.push_back(l);
        return "L" + std::to_string(list_order.size() - 1);
    };
    for (const void* l : exec) list_label(l); // the submission order names the lists
    s.log("DEPTHPROBE: capture %d, %zu events, %zu lists submitted", s.captures + 1, events.size(), exec.size());
    {
        std::string line = "DEPTHPROBE: submission order:";
        for (const void* l : exec) line += " " + list_label(l);
        s.log("%s", line.c_str());
    }
    for (const Event& e : events) {
        if (e.res) tex_label(e.res);
        if (e.other) tex_label(e.other);
    }
    for (const void* t : tex_order) {
        const auto it = textures.find(t);
        if (it == textures.end()) continue;
        s.log("DEPTHPROBE: texture %s = %p %llux%u format=%d slices=%u", tex_label(t).c_str(), t, it->second.width, it->second.height, it->second.format, it->second.array_size);
    }
    // the events, grouped by list in submission order (the order inside a list is the order the commands were recorded)
    std::vector<const void*> order = exec;
    for (const Event& e : events) {
        bool known = false;
        for (const void* l : order) known = known || l == e.list;
        if (!known) order.push_back(e.list);
    }
    int printed = 0;
    for (const void* l : order) {
        bool header = false;
        for (const Event& e : events) {
            if (e.list != l || e.kind == Kind::Exec) continue;
            if (!header) {
                s.log("DEPTHPROBE: --- list %s (%p)", list_label(l).c_str(), l);
                header = true;
            }
            if (++printed > 400) break;
            switch (e.kind) {
                case Kind::Bind: s.log("DEPTHPROBE:   bind %s as the depth target (%u render targets)", tex_label(e.res).c_str(), e.a); break;
                case Kind::Draws: s.log("DEPTHPROBE:   %u draws into %s", e.a, tex_label(e.res).c_str()); break;
                case Kind::Clear: s.log("DEPTHPROBE:   CLEAR %s flags=0x%X depth=%.3f stencil=%u", tex_label(e.res).c_str(), e.a, e.f, e.b); break;
                case Kind::Barrier:
                    s.log("DEPTHPROBE:   BARRIER %s %s -> %s", tex_label(e.res).c_str(), eldenring::d3d::describeResourceState(e.a).c_str(), eldenring::d3d::describeResourceState(e.b).c_str());
                    break;
                case Kind::Copy: s.log("DEPTHPROBE:   COPY %s <- %s", tex_label(e.res).c_str(), tex_label(e.other).c_str()); break;
                default: break;
            }
        }
    }
    if (printed > 400) s.log("DEPTHPROBE: (output cut at 400 events)");
}

// Call from Present: one frame is recorded between two calls.
inline void OnPresent() {
    State& s = S();
    if (s.start_ms == 0) return; // not installed
    const uint64_t now = GetTickCount64();
    if (s.active.load(std::memory_order_relaxed)) {
        s.active.store(false, std::memory_order_relaxed);
        Dump();
        ++s.captures;
        s.next_ms = (now - s.start_ms) + 12000;
        return;
    }
    if (s.captures < 3 && now - s.start_ms >= s.next_ms) {
        {
            std::lock_guard<std::mutex> g(s.mutex);
            s.events.clear();
            s.exec_order.clear();
            s.bound.clear();
        }
        s.active.store(true, std::memory_order_relaxed);
    }
}

} // namespace erov::probe
