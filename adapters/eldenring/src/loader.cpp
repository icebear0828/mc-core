// Elden Ring loader (issue #96): thin dinput8.dll entry point.
//
// The DLL entry boilerplate - DllMain, the loader worker thread, MinHook
// lifetime, the config/log bootstrap and the present/input hook lifecycle -
// lives in the shared mc::host::Dinput8Runtime (issue #177). This file only
// binds that runtime to the Elden Ring loader: the worker body, the hook
// installers and the log sink. Environment checks, singleton binding, hook
// installation, domain wiring and teardown all live in the loader_detail
// modules under src/core, src/hooks, src/world, src/combat, src/movement and
// src/ui.
//
// Offline only: the loader refuses to do anything when an EAC module is loaded
// or steam_appid.txt is missing (checked in LoaderThread). Configuration is
// read only after that gate, so a refused host never touches mc_er.ini.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>

#include "core/loader_internal.hpp"
#include "eldenring_config.hpp"
#include "runtime/dinput8_runtime.hpp"

namespace eldenring::loader_detail {

// MinHook is shared with the adapter's other detours (damage, camera step,
// zone probe, ...), so the host runtime must not release it on shutdown: the
// adapter keeps the engine alive for the process lifetime.

} // namespace eldenring::loader_detail

namespace {

void HostLog(const char* message) {
    eldenring::diagnostics::Log("%s", message);
}

DWORD WINAPI LoaderEntry(LPVOID param);

// Constant-initialised: DllMain must not depend on dynamically initialised state.
constinit const mc::host::Dinput8Runtime::Bindings kBindings{
    &LoaderEntry,
    &eldenring::loader_detail::EnsureMinHook,
    nullptr, // no release: see the note above
    &eldenring::loader_detail::InitLogOnce,
    &HostLog,
    &eldenring::loader_detail::InstallPresentHooks,
    &eldenring::loader_detail::UninstallPresentHooks,
    &eldenring::loader_detail::InstallInputHooks,
    &eldenring::loader_detail::UninstallInputHooks,
};

// The adapter's worker body, wrapped so the shared runtime owns its lifetime.
//
// The runtime is created here but initialised by LoaderThread only after the
// EAC/steam_appid environment gate, so configuration, logging and MinHook all
// come up in the original order. LoaderThread never returns on a healthy host;
// shutdown() runs only when the gate or the bootstrap refused to start.
DWORD WINAPI LoaderEntry(LPVOID) {
    mc::host::Dinput8Runtime runtime(kBindings);
    const DWORD result = eldenring::loader_detail::LoaderThread(&runtime);
    // LoaderThread only returns on a refused or failed bootstrap. Release any
    // DirectInput interface the proxy queued before the input hooks came up, so
    // a refused host does not leak COM references for the process lifetime.
    eldenring::loader_detail::UninstallInputHooks();
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
