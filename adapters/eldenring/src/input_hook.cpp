#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>

#include "input_hook.hpp"

namespace erin {
namespace {

enum class Kind { Mouse, Keyboard, Other };

constexpr UINT kVtblCreateDevice = 3;
constexpr UINT kVtblGetDeviceState = 9;
constexpr UINT kVtblGetDeviceData = 10;
constexpr size_t kMouseWheelOffset = 8;    // lZ: wheel delta since the last poll (WHEEL_DELTA = 120 per notch)
constexpr size_t kMouseButtonsOffset = 12; // DIMOUSESTATE / DIMOUSESTATE2: lX, lY, lZ, then rgbButtons

LogFn g_log = nullptr;
std::mutex g_mutex;
std::unordered_map<void*, Kind> g_kinds;
std::set<void*> g_hooked_functions;
std::atomic<bool> g_suppress{false};
std::atomic<bool> g_left_edge{false};
std::atomic<bool> g_left_prev{false};
std::atomic<int> g_wheel{0};
std::atomic<unsigned> g_mouse_state{0}, g_mouse_data{0}, g_keyboard_state{0}, g_keyboard_data{0}, g_other{0}, g_cleared{0};

using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);
using GetStateFn = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
using GetDataFn = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
CreateDeviceFn g_create_orig = nullptr;
GetStateFn g_state_orig = nullptr;
GetDataFn g_data_orig = nullptr;

Kind KindOf(void* device) {
    std::lock_guard<std::mutex> g(g_mutex);
    const auto it = g_kinds.find(device);
    return it == g_kinds.end() ? Kind::Other : it->second;
}

void Count(Kind k, std::atomic<unsigned>& mouse, std::atomic<unsigned>& keyboard) {
    if (k == Kind::Mouse) {
        ++mouse;
    } else if (k == Kind::Keyboard) {
        ++keyboard;
    } else {
        ++g_other;
    }
}

HRESULT STDMETHODCALLTYPE GetStateDetour(void* self, DWORD cb, LPVOID data) {
    const HRESULT hr = g_state_orig(self, cb, data);
    if (SUCCEEDED(hr)) {
        const Kind k = KindOf(self);
        Count(k, g_mouse_state, g_keyboard_state);
        if (k == Kind::Mouse && data != nullptr && cb >= kMouseButtonsOffset + 2) {
            const bool down = (static_cast<BYTE*>(data)[kMouseButtonsOffset] & 0x80) != 0;
            if (down && !g_left_prev.load()) g_left_edge.store(true);
            g_left_prev.store(down);
        }
        if (k == Kind::Mouse && data != nullptr && cb >= kMouseWheelOffset + sizeof(LONG)) {
            LONG z = 0;
            memcpy(&z, static_cast<BYTE*>(data) + kMouseWheelOffset, sizeof(z));
            if (z != 0) g_wheel.fetch_add(static_cast<int>(z));
        }
        if (k == Kind::Mouse && g_suppress.load(std::memory_order_relaxed) && data != nullptr && cb >= kMouseButtonsOffset + 2) {
            auto* buttons = static_cast<BYTE*>(data) + kMouseButtonsOffset;
            if (buttons[0] != 0 || buttons[1] != 0) ++g_cleared;
            buttons[0] = 0; // left
            buttons[1] = 0; // right
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE GetDataDetour(void* self, DWORD cb, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags) {
    const HRESULT hr = g_data_orig(self, cb, data, count, flags);
    if (SUCCEEDED(hr)) {
        const Kind k = KindOf(self);
        Count(k, g_mouse_data, g_keyboard_data);
        if (k == Kind::Mouse && g_suppress.load(std::memory_order_relaxed) && data != nullptr && count != nullptr &&
            cb >= sizeof(DIDEVICEOBJECTDATA)) {
            for (DWORD i = 0; i < *count; ++i) {
                auto* e = reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<BYTE*>(data) + static_cast<size_t>(i) * cb);
                if (e->dwOfs == DIMOFS_BUTTON0 || e->dwOfs == DIMOFS_BUTTON1) {
                    if (e->dwData != 0) ++g_cleared;
                    e->dwData = 0;
                }
            }
        }
    }
    return hr;
}

void HookDeviceFunctions(void* device) {
    void** vtable = *reinterpret_cast<void***>(device);
    struct Target {
        UINT index;
        void* detour;
        void** original;
    } targets[] = {{kVtblGetDeviceState, reinterpret_cast<void*>(&GetStateDetour), reinterpret_cast<void**>(&g_state_orig)},
                   {kVtblGetDeviceData, reinterpret_cast<void*>(&GetDataDetour), reinterpret_cast<void**>(&g_data_orig)}};
    for (const Target& t : targets) {
        void* fn = vtable[t.index];
        {
            std::lock_guard<std::mutex> g(g_mutex);
            if (!g_hooked_functions.insert(fn).second) continue;
        }
        if (*t.original != nullptr) {
            // A second, different implementation of the same method (for example the ANSI device): this build only
            // follows the first one, and says so.
            if (g_log) g_log("input: another implementation of vtable slot %u at %p is not hooked", t.index, fn);
            continue;
        }
        if (MH_CreateHook(fn, t.detour, t.original) != MH_OK || MH_EnableHook(fn) != MH_OK) {
            if (g_log) g_log("input: hooking vtable slot %u at %p failed", t.index, fn);
        } else if (g_log) {
            g_log("input: hooked device vtable slot %u at %p", t.index, fn);
        }
    }
}

HRESULT STDMETHODCALLTYPE CreateDeviceDetour(void* self, REFGUID guid, void** out, LPUNKNOWN outer) {
    const HRESULT hr = g_create_orig(self, guid, out, outer);
    if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
        const Kind k = IsEqualGUID(guid, GUID_SysMouse)      ? Kind::Mouse
                       : IsEqualGUID(guid, GUID_SysKeyboard) ? Kind::Keyboard
                                                              : Kind::Other;
        {
            std::lock_guard<std::mutex> g(g_mutex);
            g_kinds[*out] = k;
        }
        if (g_log) {
            g_log("input: CreateDevice %s -> %p", k == Kind::Mouse ? "mouse" : k == Kind::Keyboard ? "keyboard" : "other device", *out);
        }
        HookDeviceFunctions(*out);
    }
    return hr;
}

} // namespace

void SetLog(LogFn log) { g_log = log; }

void OnDirectInputCreated(REFIID riid, void* iface) {
    (void)riid; // the W and A interfaces share the slot layout
    if (iface == nullptr) return;
    void** vtable = *reinterpret_cast<void***>(iface);
    void* fn = vtable[kVtblCreateDevice];
    {
        std::lock_guard<std::mutex> g(g_mutex);
        if (!g_hooked_functions.insert(fn).second) return;
    }
    if (g_create_orig != nullptr) {
        if (g_log) g_log("input: a second CreateDevice implementation at %p is not hooked", fn);
        return;
    }
    if (MH_CreateHook(fn, reinterpret_cast<void*>(&CreateDeviceDetour), reinterpret_cast<void**>(&g_create_orig)) != MH_OK ||
        MH_EnableHook(fn) != MH_OK) {
        if (g_log) g_log("input: hooking CreateDevice at %p failed", fn);
    } else if (g_log) {
        g_log("input: hooked IDirectInput8::CreateDevice at %p", fn);
    }
}

bool TakeLeftClick() { return g_left_edge.exchange(false); }

int TakeWheelNotches() { return g_wheel.exchange(0) / 120; }

void SetSuppressMouseButtons(bool on) { g_suppress.store(on, std::memory_order_relaxed); }

Counters TakeCounters() {
    Counters c;
    c.mouse_state = g_mouse_state.exchange(0);
    c.mouse_data = g_mouse_data.exchange(0);
    c.keyboard_state = g_keyboard_state.exchange(0);
    c.keyboard_data = g_keyboard_data.exchange(0);
    c.other = g_other.exchange(0);
    c.mouse_buttons_cleared = g_cleared.exchange(0);
    return c;
}

} // namespace erin
