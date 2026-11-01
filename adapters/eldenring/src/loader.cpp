// Elden Ring loader (issue #96): thin dinput8.dll entry point.
//
// This file only owns the DLL entry points: DllMain starts the loader worker
// thread and DirectInput8Create forwards to the input proxy. Environment
// checks, singleton binding, hook installation, domain wiring and teardown all
// live in the loader_detail modules under src/core, src/hooks, src/world,
// src/combat, src/movement and src/ui.
//
// Offline only: the loader refuses to do anything when an EAC module is loaded
// or steam_appid.txt is missing (checked in LoaderThread).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>

#include "core/loader_internal.hpp"

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    return eldenring::loader_detail::DirectInput8CreateImpl(hinst, version, riid, out, outer);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, eldenring::loader_detail::LoaderThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
