// Elden Ring loader (Phase 4): thin dinput8.dll entrypoint and scheduling convergence.
// Offline only: refuses execution when EasyAntiCheat is detected or steam_appid.txt is missing.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <unknwn.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "input_hook.hpp"
#include "overlay_d3d12.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_state.hpp"
#include "eldenring_diagnostics.hpp"
#include "eldenring_memory_scanner.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_locomotion.hpp"
#include "eldenring_world_runtime.hpp"
#include "eldenring_combat_service.hpp"
#include "eldenring_inventory_controller.hpp"
#include "eldenring_debug_probes.hpp"
#include "eldenring_hud_bridge.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_hooks.hpp"

namespace {

using namespace eldenring;
using eldenring::diagnostics::Log;
using eldenring::diagnostics::InitLogOnce;
using eldenring::memory::ReadImageInfo;
using eldenring::memory::LocateSingleton;
using eldenring::memory::Bound;
using eldenring::memory::ImageInfo;
using eldenring::memory::SelfReader;
using namespace eldenring::live;

const auto& g_game_dir = eldenring::diagnostics::GetGameDir();
const SelfReader g_reader;
ImageInfo g_img;
uint32_t g_rva_world = 0, g_rva_menu = 0, g_rva_loading = 0, g_rva_fade = 0;
auto& g_ctx = eldenring::RuntimeContext::Instance();



void LogStatus() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    const uintptr_t menu = readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan);
    const uintptr_t loading = readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper);
    const uintptr_t fade = readSingleton(g_reader, g_img.base, g_rva_fade, sigs::kCSFade);
    char line[512];
    int n = snprintf(line, sizeof(line), "world=%p", reinterpret_cast<void*>(world));
    Vitals v;
    if (world != 0) {
        uint64_t player = 0;
        memory::SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player));
        if (player != 0 && readVitals(g_reader, g_img.base, static_cast<uintptr_t>(player), v)) {
            n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " hp=%d/%d", v.hp, v.max_hp);
        }
    }
    MenuState ms;
    if (readMenuState(g_reader, menu, ms)) {
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " menu[a=0x%02X b=0x%02X popup=%d]", ms.layer_mask_a, ms.layer_mask_b, ms.popup_open ? 1 : 0);
    }
    LoadingState ls;
    if (readLoadingState(g_reader, loading, ls)) {
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " load[job=%d screen=%d cnt=%d]", ls.job_active ? 1 : 0, ls.screen_loading ? 1 : 0, ls.load_counter);
    }
    float fa = 0.f;
    if (readFadeAlpha(g_reader, fade, fa)) n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " fade=%.2f", fa);
    (void)n;
    Log("%s", line);
}

DWORD WINAPI KeyLoopThread(LPVOID) {
    Log("key: KeyThread started (tid %lu)", GetCurrentThreadId());
    input::HotkeyDispatcher::Instance().runKeyLoop();
    return 0;
}

DWORD WINAPI LoaderThread(LPVOID) {
    InitLogOnce();
    std::vector<std::string> names;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const size_t count = std::min<size_t>(needed / sizeof(HMODULE), 1024);
        for (size_t i = 0; i < count; ++i) {
            char name[MAX_PATH] = {};
            if (GetModuleFileNameA(mods[i], name, MAX_PATH)) {
                std::string s = name;
                names.push_back(s.substr(s.find_last_of("\\/") + 1));
            }
        }
    }
    const EnvVerdict verdict = checkEnvironment(names, hooks::FileExists(g_game_dir + "steam_appid.txt"));
    if (verdict != EnvVerdict::Ok) {
        Log("environment check FAILED (%s): not binding anything", verdict == EnvVerdict::EacLoaded ? "EAC module loaded" : "steam_appid.txt missing");
        return 0;
    }
    Log("environment check ok (%zu modules)", names.size());

    for (int attempt = 0;; ++attempt) {
        if (ReadImageInfo(g_img)) {
            const Bound w = LocateSingleton(g_img, sigs::kWorldChrMan);
            const Bound m = LocateSingleton(g_img, sigs::kCSMenuMan);
            const Bound l = LocateSingleton(g_img, sigs::kCSNowLoadingHelper);
            const Bound f = LocateSingleton(g_img, sigs::kCSFade);
            if (w.ok && m.ok && l.ok && f.ok) {
                g_rva_world = w.rva; g_rva_menu = m.rva; g_rva_loading = l.rva; g_rva_fade = f.rva;
                Log("singletons bound: WorldChrMan=0x%X CSMenuMan=0x%X (image base %p)", w.rva, m.rva, reinterpret_cast<void*>(g_img.base));
                break;
            }
        }
        if (attempt > 120) {
            Log("giving up: singleton signatures never became unique");
            return 0;
        }
        Sleep(1000);
    }

    hooks::SetupAllDomainServices(g_img.base, g_rva_world);
    hooks::InstallAllHooks(g_game_dir, g_img.base, g_img.image_size);
    ui::SetupOverlay(g_game_dir, g_ctx);

    CreateThread(nullptr, 0, KeyLoopThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, debug::BuddyThread, nullptr, 0, nullptr);

    for (;;) {
        Sleep(1000);
        LogStatus();
    }
}

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInput8Create_t g_system_create = nullptr;
std::atomic<bool> g_wants_input_hooks{false};

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    if (g_system_create == nullptr) {
        char dir[MAX_PATH] = {};
        if (GetSystemDirectoryA(dir, MAX_PATH) > 0) {
            const std::string path = std::string(dir) + "\\dinput8.dll";
            if (HMODULE m = LoadLibraryA(path.c_str())) {
                g_system_create = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(m, "DirectInput8Create"));
            }
        }
    }
    if (g_system_create == nullptr) return E_FAIL;
    static std::once_flag decided;
    std::call_once(decided, [] {
        InitLogOnce();
        const DWORD a = GetFileAttributesA((g_game_dir + "mc_er_input.txt").c_str());
        if (a != INVALID_FILE_ATTRIBUTES && hooks::EnsureMinHook()) {
            erin::SetLog([](const char* fmt, ...) {
                char msg[512];
                va_list args;
                va_start(args, fmt);
                vsnprintf(msg, sizeof(msg), fmt, args);
                va_end(args);
                Log("%s", msg);
            });
            g_wants_input_hooks.store(true);
        }
    });
    const HRESULT hr = g_system_create(hinst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out != nullptr && *out != nullptr && g_wants_input_hooks.load()) erin::OnDirectInputCreated(riid, *out);
    return hr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
