#pragma once

// dinput8.dll proxy host runtime (issue #177, P5).
//
// The runtime owns the parts every dinput8 loader repeats: the DllMain body
// (DisableThreadLibraryCalls + CreateThread), the config and log bootstrap, the
// present/input hook registration and the hook engine they need. The
// game-specific pieces are injected as Bindings, so this file knows nothing
// about Elden Ring: the adapter keeps LoaderThread and its detours, the runtime
// keeps the entry point.
//
// The hook engine is started lazily, on the first non-empty install*Hooks()
// call, and released by shutdown(). The adapter calls install*Hooks() only
// after its environment gate, so the runtime cannot bring MinHook up ahead of
// the adapter's checks. An installation always needs a matching uninstaller:
// the runtime refuses one it could not remove.

#include <functional>
#include <string>
#include <string_view>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "mc/contracts/host_runtime.hpp"

namespace mc::host {

class Dinput8Runtime final : public IHostRuntime {
public:
    // The adapter's entry points. Every field is optional; a missing installer
    // makes the matching install*Hooks() call report failure instead of lying.
    // ensure_minhook/release_minhook are the adapter's hook engine: the runtime
    // starts it when it installs hooks itself and releases it on shutdown.
    // uninstall_* remove what install_* installed; the contract's "empty
    // callback set removes the hooks" cannot be honoured without them.
    struct Bindings {
        DWORD(WINAPI* loader_thread)(LPVOID) = nullptr;
        bool (*ensure_minhook)() = nullptr;
        bool (*release_minhook)() = nullptr;
        void (*init_log)() = nullptr;
        void (*log_line)(const char*) = nullptr;
        bool (*install_present)(const PresentCallbacks&) = nullptr;
        void (*uninstall_present)() = nullptr;
        bool (*install_input)(const InputCallbacks&) = nullptr;
        void (*uninstall_input)() = nullptr;
    };

    explicit Dinput8Runtime(Bindings bindings = {});

    Dinput8Runtime(const Dinput8Runtime&) = delete;
    Dinput8Runtime& operator=(const Dinput8Runtime&) = delete;

    // The whole DllMain body: DisableThreadLibraryCalls + CreateThread. Static
    // so DllMain never depends on an object with dynamic initialisation.
    static bool onProcessAttach(HMODULE module, const Bindings& bindings);

    bool initialize(const HostConfig& config) override;
    void shutdown() override;
    void setFrameCallback(std::function<void(float)> callback) override;
    bool installPresentHooks(const PresentCallbacks& callbacks) override;
    bool installInputHooks(const InputCallbacks& callbacks) override;
    void log(std::string_view message) override;
    [[nodiscard]] const HostConfig& config() const override { return config_; }
    [[nodiscard]] const HostRuntimeInfo& info() const override { return info_; }

private:
    // Starts the adapter's hook engine once, on demand. Returns true when the
    // engine is ready (or when the adapter binds none and owns it itself).
    bool ensureHookEngine();

    Bindings bindings_;
    HostConfig config_;
    // Derived from the bindings in the constructor: a capability bit is true
    // only when this runtime can actually perform the operation.
    HostRuntimeInfo info_;
    std::function<void(float)> frame_;
    PresentCallbacks present_;
    InputCallbacks input_;
    bool initialized_{false};
    bool minhook_owned_{false};
    bool present_installed_{false};
    bool input_installed_{false};
};

} // namespace mc::host
