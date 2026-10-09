#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <mutex>

#include "overlay_d3d12.hpp"
#include "steve_renderer_d3d12.hpp"

namespace erov {
namespace {

constexpr UINT kMaxFrames = 8;
constexpr UINT kVtblPresent = 8;
constexpr UINT kVtblResizeBuffers = 13;
constexpr UINT kVtblExecuteCommandLists = 10;
constexpr UINT kVtblDeviceCreateDsv = 21;

HudProvider g_provider = nullptr;
LogFn g_log = nullptr;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
PresentFn g_present_orig = nullptr;
ResizeFn g_resize_orig = nullptr;
ExecuteFn g_execute_orig = nullptr;
using CreateDsvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
CreateDsvFn g_create_dsv_orig = nullptr;

SteveConfig g_steve_cfg;
SteveRenderer g_steve;
std::mutex g_depth_mutex;
ID3D12Resource* g_depth_res = nullptr; // AddRef'd main scene depth (R32G8X24_TYPELESS), guarded by g_depth_mutex
UINT g_depth_w = 0, g_depth_h = 0;
std::atomic<bool> g_depth_dirty{false};
D3D12_CPU_DESCRIPTOR_HANDLE g_depth_cpu{};
D3D12_GPU_DESCRIPTOR_HANDLE g_depth_gpu{};

// The DIRECT queue the game submits on. Written by any thread inside ExecuteCommandLists.
std::atomic<ID3D12CommandQueue*> g_queue{nullptr};

struct Frame {
    ID3D12CommandAllocator* alloc{nullptr};
    ID3D12Resource* back{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    UINT64 fence_value{0};
};

struct State {
    bool ready{false};
    bool failed{false};
    IDXGISwapChain3* swap{nullptr};
    ID3D12Device* device{nullptr};
    ID3D12CommandQueue* queue{nullptr};
    ID3D12DescriptorHeap* rtv_heap{nullptr};
    ID3D12DescriptorHeap* srv_heap{nullptr};
    ID3D12GraphicsCommandList* list{nullptr};
    ID3D12Fence* fence{nullptr};
    HANDLE fence_event{nullptr};
    UINT64 fence_counter{0};
    UINT buffers{0};
    UINT width{0}, height{0};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    Frame frames[kMaxFrames];
    bool imgui_ready{false};
    std::chrono::steady_clock::time_point last_frame{};
};
State g_s;

template <typename T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void Logf(const char* msg) {
    if (g_log) g_log("%s", msg);
}

void WaitForGpu() {
    if (!g_s.queue || !g_s.fence || !g_s.fence_event) return;
    const UINT64 v = ++g_s.fence_counter;
    if (SUCCEEDED(g_s.queue->Signal(g_s.fence, v)) && g_s.fence->GetCompletedValue() < v) {
        g_s.fence->SetEventOnCompletion(v, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 2000);
    }
}

void Teardown() {
    if (g_s.ready) WaitForGpu();
    g_steve.release();
    if (g_s.imgui_ready) {
        ImGui_ImplDX12_Shutdown();
        ImGui::DestroyContext();
        g_s.imgui_ready = false;
    }
    for (Frame& f : g_s.frames) {
        SafeRelease(f.alloc);
        SafeRelease(f.back);
        f.fence_value = 0;
    }
    SafeRelease(g_s.list);
    SafeRelease(g_s.rtv_heap);
    SafeRelease(g_s.srv_heap);
    SafeRelease(g_s.fence);
    if (g_s.fence_event) {
        CloseHandle(g_s.fence_event);
        g_s.fence_event = nullptr;
    }
    SafeRelease(g_s.device);
    SafeRelease(g_s.swap);
    g_s.queue = nullptr; // not owned
    g_s.ready = false;
}

// Looks for the queue pointer inside the swap chain object, to confirm the queue we learned from ExecuteCommandLists
// is the one DXGI presents with (the layout is dxgi.dll internal: only used for the log, never for the decision).
int FindQueueOffsetInSwapChain(IDXGISwapChain* sc, ID3D12CommandQueue* q) {
    const auto* words = reinterpret_cast<const uintptr_t*>(sc);
    for (int i = 0; i < 0x100; ++i) {
        uintptr_t v = 0;
        __try {
            v = words[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
        if (v == reinterpret_cast<uintptr_t>(q)) return i * static_cast<int>(sizeof(uintptr_t));
    }
    return -1;
}

bool Init(IDXGISwapChain* sc, ID3D12CommandQueue* queue) {
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3)))) {
        Logf("overlay: swap chain is not IDXGISwapChain3");
        return false;
    }
    g_s.swap = sc3;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc3->GetDesc(&desc))) return false;
    g_s.buffers = std::min<UINT>(desc.BufferCount, kMaxFrames);
    g_s.width = desc.BufferDesc.Width;
    g_s.height = desc.BufferDesc.Height;
    g_s.format = desc.BufferDesc.Format;
    if (FAILED(sc3->GetDevice(IID_PPV_ARGS(&g_s.device)))) return false;
    // The queue must belong to the same device as the swap chain.
    ID3D12Device* qdev = nullptr;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&qdev))) || qdev != g_s.device) {
        SafeRelease(qdev);
        Logf("overlay: captured queue belongs to another device, waiting for another one");
        SafeRelease(g_s.device);
        SafeRelease(g_s.swap);
        return false;
    }
    SafeRelease(qdev);
    g_s.queue = queue;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = g_s.buffers;
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 2; // 0: ImGui font, 1: scene depth
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_s.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&g_s.rtv_heap))) ||
        FAILED(g_s.device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&g_s.srv_heap)))) {
        Logf("overlay: descriptor heaps failed");
        return false;
    }
    const UINT inc = g_s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_s.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < g_s.buffers; ++i) {
        Frame& f = g_s.frames[i];
        if (FAILED(g_s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc))) ||
            FAILED(sc3->GetBuffer(i, IID_PPV_ARGS(&f.back)))) {
            Logf("overlay: per-frame resources failed");
            return false;
        }
        f.rtv = h;
        D3D12_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = g_s.format;
        rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        g_s.device->CreateRenderTargetView(f.back, &rd, f.rtv);
        h.ptr += inc;
    }
    if (FAILED(g_s.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_s.frames[0].alloc, nullptr,
                                             IID_PPV_ARGS(&g_s.list)))) {
        Logf("overlay: command list failed");
        return false;
    }
    g_s.list->Close();
    if (FAILED(g_s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_s.fence)))) return false;
    g_s.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_s.fence_event) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplDX12_Init(g_s.device, static_cast<int>(g_s.buffers), g_s.format, g_s.srv_heap,
                             g_s.srv_heap->GetCPUDescriptorHandleForHeapStart(), g_s.srv_heap->GetGPUDescriptorHandleForHeapStart())) {
        ImGui::DestroyContext();
        Logf("overlay: ImGui_ImplDX12_Init failed");
        return false;
    }
    g_s.imgui_ready = true;
    {
        const UINT inc = g_s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_depth_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
        g_depth_cpu.ptr += inc;
        g_depth_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
        g_depth_gpu.ptr += inc;
        g_steve.setDepthView(g_s.device, nullptr, g_depth_cpu);
        if (!g_steve.init(g_s.device, g_s.format, g_log)) Logf("overlay: Steve renderer unavailable");
        g_depth_dirty.store(true); // bind the depth captured so far
    }
    g_s.last_frame = std::chrono::steady_clock::now();
    g_s.ready = true;
    char msg[256];
    snprintf(msg, sizeof(msg), "overlay: ready (buffers=%u, %ux%u, format=%d, queue=%p, queue found in swap chain at +0x%X)", g_s.buffers,
             g_s.width, g_s.height, static_cast<int>(g_s.format), static_cast<void*>(queue),
             static_cast<unsigned>(std::max(0, FindQueueOffsetInSwapChain(sc, queue))));
    Logf(msg);
    return true;
}

void DrawHud(const HudState& hud, float w, float h) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float scale = std::max(0.5f, h / 1080.f);
    const float cell = 20.f * scale;
    const float gap = 2.f * scale;
    const float slot = 44.f * scale;
    const float hotbar_w = 9.f * slot;
    const float x0 = (w - hotbar_w) * 0.5f;
    const float y_bar = h - slot - 8.f * scale;
    // hotbar
    for (int i = 0; i < 9; ++i) {
        const float x = x0 + static_cast<float>(i) * slot;
        dl->AddRectFilled({x, y_bar}, {x + slot, y_bar + slot}, IM_COL32(0, 0, 0, 120));
        const bool sel = i == hud.selected_slot;
        dl->AddRect({x, y_bar}, {x + slot, y_bar + slot}, sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(110, 110, 110, 255),
                    0.f, 0, (sel ? 3.f : 1.5f) * scale);
    }
    // hearts: 10 hearts = 20 half hearts, from the real HP
    const float ratio = hud.max_hp > 0 ? std::clamp(static_cast<float>(hud.hp) / static_cast<float>(hud.max_hp), 0.f, 1.f) : 0.f;
    const int halves = static_cast<int>(std::ceil(ratio * 20.f));
    const float y_hearts = y_bar - cell - 6.f * scale;
    for (int i = 0; i < 10; ++i) {
        const float x = x0 + static_cast<float>(i) * (cell + gap);
        dl->AddRectFilled({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(40, 0, 0, 200));
        const int fill = std::clamp(halves - i * 2, 0, 2);
        if (fill >= 1) {
            const float fw = fill == 2 ? cell : cell * 0.5f;
            dl->AddRectFilled({x, y_hearts}, {x + fw, y_hearts + cell}, IM_COL32(205, 30, 30, 255));
        }
        dl->AddRect({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.5f * scale);
    }
    // crosshair
    const float cx = w * 0.5f, cy = h * 0.5f, arm = 10.f * scale;
    dl->AddLine({cx - arm, cy}, {cx + arm, cy}, IM_COL32(255, 255, 255, 200), 2.f * scale);
    dl->AddLine({cx, cy - arm}, {cx, cy + arm}, IM_COL32(255, 255, 255, 200), 2.f * scale);
    char buf[64];
    snprintf(buf, sizeof(buf), "MC %d/%d", hud.hp, hud.max_hp);
    dl->AddText({x0, y_hearts - 18.f * scale}, IM_COL32(255, 255, 255, 220), buf);
}

void RenderFrame(IDXGISwapChain* sc) {
    HudState hud;
    SteveState steve;
    if (!g_provider || !g_provider(hud, steve) || !hud.show || !hud.mc_mode) return;

    if (g_depth_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_depth_mutex);
        g_steve.setDepthView(g_s.device, g_depth_res, g_depth_cpu);
    }

    const UINT idx = g_s.swap->GetCurrentBackBufferIndex();
    if (idx >= g_s.buffers) return;
    Frame& f = g_s.frames[idx];
    if (f.fence_value != 0 && g_s.fence->GetCompletedValue() < f.fence_value) {
        g_s.fence->SetEventOnCompletion(f.fence_value, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 1000);
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::max(1.f / 1000.f, std::chrono::duration<float>(now - g_s.last_frame).count());
    g_s.last_frame = now;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(g_s.width), static_cast<float>(g_s.height)};
    io.DeltaTime = dt;
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    DrawHud(hud, io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Render();

    f.alloc->Reset();
    g_s.list->Reset(f.alloc, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = f.back;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_s.list->ResourceBarrier(1, &b);
    g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {g_s.srv_heap};
    g_s.list->SetDescriptorHeaps(1, heaps);
    if (steve.draw && g_steve.ready()) {
        const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
        const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
        const auto parts = eldenring::render::restPoseMatrices({steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw);
        SteveParams sp;
        sp.occlusion = g_steve_cfg.occlusion && g_depth_res != nullptr;
        sp.depth_const = g_steve_cfg.depth_const;
        sp.rel_bias = g_steve_cfg.rel_bias;
        sp.abs_bias = g_steve_cfg.abs_bias;
        sp.depth_w = static_cast<float>(g_depth_w);
        sp.depth_h = static_cast<float>(g_depth_h);
        g_steve.draw(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp, parts, sp);
    }
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_s.list);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_s.list->ResourceBarrier(1, &b);
    g_s.list->Close();
    ID3D12CommandList* lists[] = {g_s.list};
    g_execute_orig(g_s.queue, 1, lists);
    f.fence_value = ++g_s.fence_counter;
    g_s.queue->Signal(g_s.fence, f.fence_value);
    (void)sc;
}

void STDMETHODCALLTYPE CreateDsvDetour(ID3D12Device* d, ID3D12Resource* res, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
                                       D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    g_create_dsv_orig(d, res, desc, handle);
    if (res == nullptr) return;
    const D3D12_RESOURCE_DESC rd = res->GetDesc();
    if (rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd.Format != DXGI_FORMAT_R32G8X24_TYPELESS || rd.Width < 1024 ||
        rd.DepthOrArraySize != 1) {
        return;
    }
    {
        std::lock_guard<std::mutex> g(g_depth_mutex);
        if (g_depth_res == res) return;
        res->AddRef();
        if (g_depth_res) g_depth_res->Release();
        g_depth_res = res;
        g_depth_w = static_cast<UINT>(rd.Width);
        g_depth_h = rd.Height;
    }
    g_depth_dirty.store(true);
    static int logged = 0;
    if (logged++ < 20 && g_log) {
        g_log("overlay: scene depth captured: %p %llux%u format=%d samples=%u flags=0x%X dsv_format=%d", static_cast<void*>(res),
              static_cast<unsigned long long>(rd.Width), rd.Height, static_cast<int>(rd.Format), rd.SampleDesc.Count,
              static_cast<unsigned>(rd.Flags), desc ? static_cast<int>(desc->Format) : -1);
    }
}

void STDMETHODCALLTYPE ExecuteDetour(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (g_queue.load(std::memory_order_relaxed) == nullptr) {
        const D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) g_queue.store(q);
    }
    g_execute_orig(q, n, lists);
}

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!g_s.failed) {
        ID3D12CommandQueue* q = g_queue.load();
        if (!g_s.ready && q != nullptr) {
            if (!Init(sc, q)) {
                // Init may fail only because the first captured queue is the wrong device; give up after it released everything.
                Teardown();
                static int attempts = 0;
                if (++attempts >= 20) {
                    g_s.failed = true;
                    Logf("overlay: giving up after repeated init failures");
                }
            }
        }
        if (g_s.ready) {
            __try {
                RenderFrame(sc);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_s.failed = true;
                Logf("overlay: exception while drawing, overlay disabled");
            }
        }
    }
    return g_present_orig(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE ResizeDetour(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    if (g_s.ready) {
        Teardown(); // rebuilt on the next Present
        Logf("overlay: resize, resources released");
    }
    return g_resize_orig(sc, count, w, h, fmt, flags);
}

// Throw-away device, queue and swap chain, only to read the vtable function addresses.
bool ResolveTargets(void*& present, void*& resize, void*& execute, void*& create_dsv) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"mc_er_dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"mc", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    bool ok = false;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* q = nullptr;
    IDXGIFactory4* fac = nullptr;
    IDXGISwapChain1* sc1 = nullptr;
    if (hwnd && SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))) {
            DXGI_SWAP_CHAIN_DESC1 sd{};
            sd.Width = 8;
            sd.Height = 8;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (SUCCEEDED(fac->CreateSwapChainForHwnd(q, hwnd, &sd, nullptr, nullptr, &sc1))) {
                void** sv = *reinterpret_cast<void***>(sc1);
                void** qv = *reinterpret_cast<void***>(q);
                present = sv[kVtblPresent];
                resize = sv[kVtblResizeBuffers];
                execute = qv[kVtblExecuteCommandLists];
                create_dsv = (*reinterpret_cast<void***>(dev))[kVtblDeviceCreateDsv];
                ok = true;
            }
        }
    }
    SafeRelease(sc1);
    SafeRelease(fac);
    SafeRelease(q);
    SafeRelease(dev);
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

} // namespace

void SetSteveConfig(const SteveConfig& cfg) { g_steve_cfg = cfg; }

bool Install(HudProvider provider, LogFn log) {
    g_provider = provider;
    g_log = log;
    void *present = nullptr, *resize = nullptr, *execute = nullptr, *create_dsv = nullptr;
    if (!ResolveTargets(present, resize, execute, create_dsv)) {
        Logf("overlay: could not resolve the D3D12/DXGI vtable functions");
        return false;
    }
    if (MH_CreateHook(execute, reinterpret_cast<void*>(&ExecuteDetour), reinterpret_cast<void**>(&g_execute_orig)) != MH_OK ||
        MH_CreateHook(present, reinterpret_cast<void*>(&PresentDetour), reinterpret_cast<void**>(&g_present_orig)) != MH_OK ||
        MH_CreateHook(resize, reinterpret_cast<void*>(&ResizeDetour), reinterpret_cast<void**>(&g_resize_orig)) != MH_OK ||
        MH_CreateHook(create_dsv, reinterpret_cast<void*>(&CreateDsvDetour), reinterpret_cast<void**>(&g_create_dsv_orig)) != MH_OK) {
        Logf("overlay: MH_CreateHook failed");
        return false;
    }
    if (MH_EnableHook(execute) != MH_OK || MH_EnableHook(present) != MH_OK || MH_EnableHook(resize) != MH_OK ||
        MH_EnableHook(create_dsv) != MH_OK) {
        Logf("overlay: MH_EnableHook failed");
        return false;
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "overlay: hooks installed (Present=%p ResizeBuffers=%p ExecuteCommandLists=%p CreateDepthStencilView=%p)", present,
             resize, execute, create_dsv);
    Logf(msg);
    return true;
}

} // namespace erov
