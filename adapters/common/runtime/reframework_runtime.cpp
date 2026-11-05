#include "runtime/reframework_runtime.hpp"

#include <utility>

namespace mc::host {

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
    log(compiled_with_re4() ? "reframework: runtime ready (MC_ENABLE_RE4=1)"
                            : "reframework: runtime ready (MC_ENABLE_RE4=0, skeleton only)");
    return true;
}

void ReframeworkRuntime::shutdown() {
    if (!initialized_) return;
    frame_ = nullptr;
    if (api_ != nullptr) api_->shutdown();
    initialized_ = false;
    log("reframework: shut down");
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
    // API vtable (onFrame), not from a detour it installs itself.
    (void)callbacks;
    log("reframework: present hooks are delivered by REFramework; a swapchain detour is not available");
    return false;
}

bool ReframeworkRuntime::installInputHooks(const InputCallbacks& callbacks) {
    // Same for DirectInput: REFramework pumps input and hands it to the plugin.
    (void)callbacks;
    log("reframework: input hooks are delivered by REFramework; a DirectInput detour is not available");
    return false;
}

void ReframeworkRuntime::log(std::string_view message) {
    if (api_ != nullptr) {
        const std::string line(message);
        api_->log(line.c_str());
        return;
    }
    if (sink_) sink_(message);
}

} // namespace mc::host
