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
// installPresentHooks()/installInputHooks() with an empty callback set removes
// the hooks installed earlier, and shutdown() removes anything still installed.

#include <functional>
#include <string>
#include <string_view>

namespace mc::host {

// Everything a host needs before the first frame. The adapter fills this from
// its own configuration (mc_er.ini for Elden Ring, REFramework settings for RE4).
struct HostConfig {
    std::string game_dir; // install root, with a trailing separator
    bool enable_overlay{false};
    bool enable_input{false};
    bool enable_damage{false};
    bool enable_bullet_fire{false};
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
    std::function<void()> on_direct_input_created;

    [[nodiscard]] bool empty() const noexcept { return !on_direct_input_created; }
};

class IHostRuntime {
public:
    virtual ~IHostRuntime() = default;

    // Brings up config, logging and the hook engine. False means the adapter
    // must not install anything.
    virtual bool initialize(const HostConfig& config) = 0;

    // Removes every installed hook and releases the hook engine. Idempotent.
    virtual void shutdown() = 0;

    // Called once per rendered frame; delta in seconds. An empty callback clears it.
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
