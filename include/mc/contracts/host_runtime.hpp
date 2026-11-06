#pragma once

// Host runtime seam (issue #177, P5).
//
// Every host loader repeats the same boilerplate: a DllMain that starts a worker
// thread, MinHook lifetime, the config + log bootstrap and the present/input hook
// installation. This contract is the seam that keeps an adapter's detours and
// domain wiring where they are while the entry-point plumbing becomes shared:
//
//   * mc::host::Dinput8Runtime      - the classic dinput8.dll proxy (Elden Ring)
//   * mc::host::ReframeworkRuntime  - Resident Evil 4 via REFramework (skeleton)
//
// A host that fits neither can implement IHostRuntime directly; the tests drive
// the same contract with a fake.
//
// Lifecycle contract:
//   initialize()  -> setFrameCallback() -> install*Hooks() -> shutdown()
// initialize() brings up configuration and logging only. A host starts its hook
// engine when the first non-empty install*Hooks() call arrives, so an adapter
// that installs its own detours keeps full control of the ordering (for example
// running its environment checks before MinHook is initialized).
// installPresentHooks()/installInputHooks() with an empty callback set removes
// the hooks installed earlier and succeeds even on a host that cannot install
// detours, and shutdown() removes anything still installed.

#include <functional>
#include <string>
#include <string_view>

namespace mc::host {

// Everything a host needs before the first frame. The adapter fills this from
// its own configuration (mc_er.ini for Elden Ring, REFramework settings for RE4).
// Game-specific switches (damage, bullets, ...) stay in the adapter's own config;
// this struct carries only what every host understands.
struct HostConfig {
    // Install root, with a trailing separator. The adapter must resolve it before
    // calling initialize(): a loader that derives it from its own log bootstrap
    // has to run that bootstrap first, or the host owns an empty root and reads
    // its configuration from the process CWD instead.
    std::string game_dir;
    // The adapter wants the host to install present/resize hooks.
    bool enable_overlay{false};
    // The adapter wants the host to install input hooks.
    bool enable_input{false};
};

struct HostRuntimeInfo {
    std::string name;
    std::string version;
    bool supports_present_hooks{false};
    bool supports_input_hooks{false};
};

// Per-frame and resize notifications from the host's present path.
struct PresentCallbacks {
    std::function<void()> on_present;
    std::function<void()> on_resize;

    [[nodiscard]] bool empty() const noexcept { return !on_present && !on_resize; }
};

// Notifications from the host's input path.
struct InputCallbacks {
    // Called when the host creates its input object. `iface` is the host's
    // object (for a dinput8 proxy, the IDirectInput8 the game just created);
    // the adapter owns its layout and lifetime. Passing the object instead of a
    // bare notification is what lets a proxy hook the per-device vtable at the
    // moment it appears.
    std::function<void(void* iface)> on_direct_input_created;

    [[nodiscard]] bool empty() const noexcept { return !on_direct_input_created; }
};

class IHostRuntime {
public:
    virtual ~IHostRuntime() = default;

    // Brings up configuration and logging. The hook engine starts with the
    // first non-empty install*Hooks() call, not here. False means the adapter
    // must not install anything.
    virtual bool initialize(const HostConfig& config) = 0;

    // Removes every installed hook and releases the hook engine. Idempotent.
    virtual void shutdown() = 0;

    // Called once per rendered frame; delta in seconds. An empty callback clears
    // it. A host delivers it from its present path, so the adapter must also
    // install present hooks for frames to arrive.
    virtual void setFrameCallback(std::function<void(float)> callback) = 0;

    // Installs the present/resize notifications. An empty set uninstalls them.
    virtual bool installPresentHooks(const PresentCallbacks& callbacks) = 0;

    // Installs the input notifications. An empty set uninstalls them.
    virtual bool installInputHooks(const InputCallbacks& callbacks) = 0;

    // Runtime diagnostics, forwarded to the host's log sink.
    virtual void log(std::string_view message) = 0;

    [[nodiscard]] virtual const HostConfig& config() const = 0;
    [[nodiscard]] virtual const HostRuntimeInfo& info() const = 0;
};

} // namespace mc::host
