#include "runtime/dinput8_runtime.hpp"

#include <utility>

namespace mc::host {

namespace {

// Before the adapter's log is up (Bindings::init_log) messages can only go to
// the debugger; after that Bindings::log_line forwards them to the game log.
void DebugLog(std::string_view message) {
    const std::string line = std::string(message) + "\n";
    OutputDebugStringA(line.c_str());
}

std::string DescribeConfig(const HostConfig& config) {
    std::string line = "dinput8: runtime ready (game_dir=";
    line += config.game_dir;
    line += ", overlay=";
    line += config.enable_overlay ? "on" : "off";
    line += ", input=";
    line += config.enable_input ? "on" : "off";
    line += ", damage=";
    line += config.enable_damage ? "on" : "off";
    line += ")";
    return line;
}

} // namespace

Dinput8Runtime::Dinput8Runtime(Bindings bindings) : bindings_(bindings) {}

bool Dinput8Runtime::onProcessAttach(HMODULE module, const Bindings& bindings) {
    if (module != nullptr) DisableThreadLibraryCalls(module);
    if (bindings.loader_thread == nullptr) return false;
    const HANDLE thread = CreateThread(nullptr, 0, bindings.loader_thread, nullptr, 0, nullptr);
    if (thread == nullptr) return false;
    CloseHandle(thread);
    return true;
}

bool Dinput8Runtime::initialize(const HostConfig& config) {
    if (initialized_) return true;
    config_ = config;
    if (bindings_.init_log != nullptr) bindings_.init_log();
    if (config_.enable_input && bindings_.ensure_minhook != nullptr) {
        minhook_owned_ = bindings_.ensure_minhook();
        if (!minhook_owned_) log("dinput8: MinHook initialization failed");
    }
    initialized_ = true;
    log(DescribeConfig(config_));
    return true;
}

void Dinput8Runtime::shutdown() {
    if (!initialized_) return;
    frame_ = nullptr;
    present_ = {};
    input_ = {};
    if (minhook_owned_ && bindings_.release_minhook != nullptr) {
        minhook_owned_ = false;
        bindings_.release_minhook();
    }
    initialized_ = false;
    log("dinput8: shut down");
}

void Dinput8Runtime::setFrameCallback(std::function<void(float)> callback) {
    frame_ = std::move(callback);
}

bool Dinput8Runtime::installPresentHooks(const PresentCallbacks& callbacks) {
    if (callbacks.empty()) {
        present_ = {};
        return true;
    }
    if (bindings_.install_present == nullptr) {
        log("dinput8: no present-hook installer is bound");
        return false;
    }
    if (!bindings_.install_present(callbacks)) return false;
    present_ = callbacks;
    return true;
}

bool Dinput8Runtime::installInputHooks(const InputCallbacks& callbacks) {
    if (callbacks.empty()) {
        input_ = {};
        return true;
    }
    if (bindings_.install_input == nullptr) {
        log("dinput8: no input-hook installer is bound");
        return false;
    }
    if (!bindings_.install_input(callbacks)) return false;
    input_ = callbacks;
    return true;
}

void Dinput8Runtime::log(std::string_view message) {
    if (bindings_.log_line != nullptr) {
        const std::string line(message);
        bindings_.log_line(line.c_str());
        return;
    }
    DebugLog(message);
}

} // namespace mc::host
