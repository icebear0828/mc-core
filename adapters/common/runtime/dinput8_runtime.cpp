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
    line += ")";
    return line;
}

} // namespace

Dinput8Runtime::Dinput8Runtime(Bindings bindings)
    : bindings_(bindings),
      info_{"dinput8", "1", bindings.install_present != nullptr && bindings.uninstall_present != nullptr,
            bindings.install_input != nullptr && bindings.uninstall_input != nullptr} {}

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
    initialized_ = true;
    log(DescribeConfig(config_));
    return true;
}

void Dinput8Runtime::shutdown() {
    if (!initialized_) return;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        frame_ = nullptr;
    }
    // Remove what is still installed before the hook engine goes away: a
    // detour that outlives MinHook would jump through a freed trampoline.
    bool removed = true;
    if (present_installed_) {
        if (bindings_.uninstall_present != nullptr) {
            bindings_.uninstall_present();
        } else {
            removed = false;
            log("dinput8: present hooks are still installed but no uninstaller is bound");
        }
    }
    if (input_installed_) {
        if (bindings_.uninstall_input != nullptr) {
            bindings_.uninstall_input();
        } else {
            removed = false;
            log("dinput8: input hooks are still installed but no uninstaller is bound");
        }
    }
    present_installed_ = false;
    input_installed_ = false;
    present_ = {};
    input_ = {};
    if (minhook_owned_ && bindings_.release_minhook != nullptr) {
        if (removed) {
            minhook_owned_ = false;
            bindings_.release_minhook();
        } else {
            log("dinput8: keeping the hook engine alive because installed hooks could not be removed");
        }
    }
    initialized_ = false;
    log("dinput8: shut down");
}

void Dinput8Runtime::setFrameCallback(std::function<void(float)> callback) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    frame_ = std::move(callback);
}

void Dinput8Runtime::deliverFrame() {
    std::function<void(float)> callback;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        callback = frame_;
    }
    if (!callback) return;
    const auto now = std::chrono::steady_clock::now();
    float delta_seconds = 0.f;
    if (have_last_frame_) {
        delta_seconds = std::chrono::duration<float>(now - last_frame_).count();
    }
    last_frame_ = now;
    have_last_frame_ = true;
    callback(delta_seconds);
}

bool Dinput8Runtime::ensureHookEngine() {
    if (minhook_owned_) return true;
    // No binding means the adapter owns the engine itself.
    if (bindings_.ensure_minhook == nullptr) return true;
    minhook_owned_ = bindings_.ensure_minhook();
    if (!minhook_owned_) log("dinput8: MinHook initialization failed");
    return minhook_owned_;
}

bool Dinput8Runtime::installPresentHooks(const PresentCallbacks& callbacks) {
    if (callbacks.empty()) {
        if (!present_installed_) {
            present_ = {};
            return true;
        }
        if (bindings_.uninstall_present == nullptr) {
            log("dinput8: present hooks are installed but no uninstaller is bound");
            return false;
        }
        bindings_.uninstall_present();
        present_installed_ = false;
        present_ = {};
        return true;
    }
    if (!config_.enable_overlay) {
        log("dinput8: present hooks are disabled by the config");
        return false;
    }
    // An installation the runtime cannot remove violates the contract, so it is
    // refused up front rather than discovered at shutdown.
    if (bindings_.install_present == nullptr || bindings_.uninstall_present == nullptr) {
        log("dinput8: present hooks need both an installer and an uninstaller");
        return false;
    }
    if (!ensureHookEngine()) return false;
    // Replacing the callbacks must not leave the first install's detours behind
    // (MinHook would reject the second CreateHook and the old hooks would stay
    // live while the runtime believes it installed the new set).
    if (present_installed_) {
        bindings_.uninstall_present();
        present_installed_ = false;
        present_ = {};
    }
    // The per-frame callback is delivered from the present notification, which is
    // the only per-frame signal a dinput8 proxy owns. The wrapper reads frame_ at
    // call time, so setFrameCallback() after the install still takes effect.
    PresentCallbacks effective = callbacks;
    if (callbacks.on_present) {
        const std::function<void()> user_present = callbacks.on_present;
        effective.on_present = [this, user_present]() {
            user_present();
            deliverFrame();
        };
    }
    if (!bindings_.install_present(effective)) return false;
    present_ = callbacks;
    present_installed_ = true;
    return true;
}

bool Dinput8Runtime::installInputHooks(const InputCallbacks& callbacks) {
    if (callbacks.empty()) {
        if (!input_installed_) {
            input_ = {};
            return true;
        }
        if (bindings_.uninstall_input == nullptr) {
            log("dinput8: input hooks are installed but no uninstaller is bound");
            return false;
        }
        bindings_.uninstall_input();
        input_installed_ = false;
        input_ = {};
        return true;
    }
    if (!config_.enable_input) {
        log("dinput8: input hooks are disabled by the config");
        return false;
    }
    if (bindings_.install_input == nullptr || bindings_.uninstall_input == nullptr) {
        log("dinput8: input hooks need both an installer and an uninstaller");
        return false;
    }
    if (!ensureHookEngine()) return false;
    // Input hooks are callback registrations on an already-hooked device: a
    // replacement only updates the callback, so the existing devices stay
    // hooked. Tearing them down would leave nothing to re-hook from.
    if (input_installed_) {
        if (!bindings_.install_input(callbacks)) return false;
        input_ = callbacks;
        return true;
    }
    if (!bindings_.install_input(callbacks)) return false;
    input_ = callbacks;
    input_installed_ = true;
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
