#include "runtime/reframework_runtime.hpp"

#include <utility>

namespace mc::host {

void ReframeworkRuntime::log(std::string_view message) {
    if (api_ != nullptr) {
        const std::string line(message);
        api_->log(line.c_str());
        return;
    }
    if (sink_) sink_(message);
}

// MC_ENABLE_RE4 (default OFF) selects whether the Resident Evil 4 integration is
// compiled in. The translation unit itself is always built: a static library
// needs a source on hosts without a dinput8 loader, and the stub below keeps the
// seam buildable and testable everywhere. Only the option turns it into a
// working host.
#if MC_ENABLE_RE4

bool ReframeworkRuntime::initialize(const HostConfig& config) {
    if (initialized_) return true;
    config_ = config;
    if (api_ == nullptr) {
        log("reframework: no IReframeworkHostApi injected; refusing to initialize");
        return false;
    }
    if (!api_->initialize()) {
        log("reframework: host API initialization failed");
        return false;
    }
    initialized_ = true;
    log("reframework: runtime ready");
    return true;
}

void ReframeworkRuntime::shutdown() {
    if (!initialized_) return;
    frame_ = nullptr;
    // Log while the host API is still alive: api_->shutdown() may release the
    // object and the line must not touch it afterwards.
    log("reframework: shut down");
    if (api_ != nullptr) api_->shutdown();
    initialized_ = false;
}

void ReframeworkRuntime::setFrameCallback(std::function<void(float)> callback) {
    frame_ = std::move(callback);
}

void ReframeworkRuntime::onHostFrame(float delta_seconds) {
    if (!initialized_) return;
    if (api_ != nullptr) api_->onFrame(delta_seconds);
    if (frame_) frame_(delta_seconds);
}

bool ReframeworkRuntime::installPresentHooks(const PresentCallbacks& callbacks) {
    // REFramework owns the swapchain: a plugin gets frame notifications from the
    // API vtable (onFrame), not from a detour it installs itself. Removing the
    // callbacks is still an empty success, per the contract.
    if (callbacks.empty()) return true;
    log("reframework: present hooks are delivered by REFramework; a swapchain detour is not available");
    return false;
}

bool ReframeworkRuntime::installInputHooks(const InputCallbacks& callbacks) {
    // Same for DirectInput: REFramework pumps input and hands it to the plugin.
    if (callbacks.empty()) return true;
    log("reframework: input hooks are delivered by REFramework; a DirectInput detour is not available");
    return false;
}

#else // MC_ENABLE_RE4

bool ReframeworkRuntime::initialize(const HostConfig& config) {
    if (initialized_) return true;
    config_ = config;
    if (api_ == nullptr) {
        log("reframework: no IReframeworkHostApi injected; refusing to initialize");
        return false;
    }
    log("reframework: built without MC_ENABLE_RE4; the Resident Evil 4 integration is not compiled in");
    return false;
}

void ReframeworkRuntime::shutdown() {}

void ReframeworkRuntime::setFrameCallback(std::function<void(float)>) {}

void ReframeworkRuntime::onHostFrame(float) {}

bool ReframeworkRuntime::installPresentHooks(const PresentCallbacks& callbacks) {
    if (callbacks.empty()) return true;
    log("reframework: built without MC_ENABLE_RE4; present hooks are not available");
    return false;
}

bool ReframeworkRuntime::installInputHooks(const InputCallbacks& callbacks) {
    if (callbacks.empty()) return true;
    log("reframework: built without MC_ENABLE_RE4; input hooks are not available");
    return false;
}

#endif // MC_ENABLE_RE4

} // namespace mc::host
