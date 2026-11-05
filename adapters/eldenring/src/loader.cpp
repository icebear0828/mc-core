// Elden Ring loader (issue #96): thin dinput8.dll entry point.
//
// The DLL entry boilerplate - DllMain, the loader worker thread, MinHook
// lifetime and the config/log bootstrap - lives in the shared
// mc::host::Dinput8Runtime (issue #177). This file only binds that runtime to
// the Elden Ring loader: the worker body, the hook installers and the log sink.
// Environment checks, singleton binding, hook installation, domain wiring and
// teardown all live in the loader_detail modules under src/core, src/hooks,
// src/world, src/combat, src/movement and src/ui.
//
// Offline only: the loader refuses to do anything when an EAC module is loaded
// or steam_appid.txt is missing (checked in LoaderThread).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>

#include <string>

#include "core/loader_internal.hpp"
#include "eldenring_config.hpp"
#include "runtime/dinput8_runtime.hpp"

namespace eldenring::loader_detail {

// MinHook teardown. The shared runtime owns the lifetime; the adapter supplies
// the call because it is the side that also initializes MinHook.
bool ReleaseMinHook() {
    return MH_Uninitialize() == MH_OK;
}

} // namespace eldenring::loader_detail

namespace {

// The adapter's configuration, resolved into the runtime's portable shape.
mc::host::HostConfig LoadHostConfig() {
    // InitLogOnce resolves g_game_dir from the module path. Without it the
    // install root is still empty here, the runtime would own an empty game_dir
    // and EnsureLoaded() would read mc_er.ini relative to the process CWD.
    // It is idempotent, so the runtime and LoaderThread may call it again.
    eldenring::loader_detail::InitLogOnce();
    mc::host::HostConfig config;
    config.game_dir = eldenring::loader_detail::g_game_dir;
    eldenring::AdapterConfig& adapter = eldenring::AdapterConfig::Instance();
    adapter.EnsureLoaded(config.game_dir);
    config.enable_overlay = adapter.FeatureEnabled(eldenring::ConfigFeature::Overlay);
    config.enable_input = adapter.FeatureEnabled(eldenring::ConfigFeature::Input);
    // Damage and bullet-fire are Elden Ring gameplay switches; the adapter reads
    // them from AdapterConfig where it needs them, not through the host seam.
    return config;
}

void HostLog(const char* message) {
    eldenring::diagnostics::Log("%s", message);
}

DWORD WINAPI LoaderEntry(LPVOID param);

// Constant-initialised: DllMain must not depend on dynamically initialised state.
constinit const mc::host::Dinput8Runtime::Bindings kBindings{
    &LoaderEntry,
    &eldenring::loader_detail::EnsureMinHook,
    &eldenring::loader_detail::ReleaseMinHook,
    &eldenring::loader_detail::InitLogOnce,
    &HostLog,
    nullptr, // present hooks are still installed by SetupOverlay() in LoaderThread
    nullptr, // input hooks are still installed by SetupInput() in LoaderThread
};

// The adapter's worker body, wrapped so the shared runtime owns its lifetime.
//
// The runtime starts MinHook only when it installs hooks itself (install_present
// /install_input are unbound here), so the Elden Ring detours keep initialising
// it from SetupOverlay()/SetupInput() after LoaderThread's EAC/steam_appid
// environment gate - the same ordering as before the runtime was extracted.
DWORD WINAPI LoaderEntry(LPVOID param) {
    mc::host::Dinput8Runtime runtime(kBindings);
    runtime.initialize(LoadHostConfig());
    const DWORD result = eldenring::loader_detail::LoaderThread(param);
    runtime.shutdown();
    return result;
}

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    return eldenring::loader_detail::DirectInput8CreateImpl(hinst, version, riid, out, outer);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // A failed thread start must not unload the proxy: the game keeps running
        // without the mod, exactly as it did before the runtime was extracted.
        (void)mc::host::Dinput8Runtime::onProcessAttach(module, kBindings);
    }
    return TRUE;
}
