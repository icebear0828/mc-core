#pragma once

#include "mc/contracts/mod_interface.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc::contracts {

class ModManager {
public:
    ModManager() = default;
    ~ModManager() {
        shutdownAll();
    }

    // Non-copyable
    ModManager(const ModManager&) = delete;
    ModManager& operator=(const ModManager&) = delete;

    bool registerMod(std::shared_ptr<IModInstance> mod) {
        if (!mod || !mod->name()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        std::string mod_name = mod->name();
        for (const auto& entry : mods_) {
            if (entry.instance->name() == mod_name) {
                return false; // Name collision
            }
        }

        ModEntry entry;
        entry.instance = mod;
        entry.enabled = true;
        entry.initialized = false;
        mods_.push_back(std::move(entry));

        sortModsLocked();
        return true;
    }

    bool unregisterMod(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(mods_.begin(), mods_.end(), [&](const ModEntry& e) {
            return e.instance->name() == name;
        });

        if (it != mods_.end()) {
            if (it->initialized) {
                it->instance->onShutdown();
            }
            mods_.erase(it);
            return true;
        }
        return false;
    }

    size_t modCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return mods_.size();
    }

    bool setModEnabled(const std::string& name, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mods_) {
            if (entry.instance->name() == name) {
                entry.enabled = enabled;
                return true;
            }
        }
        return false;
    }

    bool isModEnabled(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : mods_) {
            if (entry.instance->name() == name) {
                return entry.enabled;
            }
        }
        return false;
    }

    bool initializeAll(CoreHostServices& services) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool all_ok = true;
        for (auto& entry : mods_) {
            if (!entry.initialized) {
                if (entry.instance->onInitialize(services)) {
                    entry.initialized = true;
                } else {
                    all_ok = false;
                }
            }
        }
        return all_ok;
    }

    void shutdownAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Higher-priority gameplay mods depend on underlying systems,
        // so higher-priority mods must be torn down first.
        for (auto& entry : mods_) {
            if (entry.initialized) {
                entry.instance->onShutdown();
                entry.initialized = false;
            }
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mods_) {
            if (entry.initialized) {
                entry.instance->onShutdown();
                entry.initialized = false;
            }
        }
        mods_.clear();
    }

    void tick(float dt) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mods_) {
            if (entry.enabled && entry.initialized) {
                entry.instance->onTick(dt);
            }
        }
    }

    void render2D(void* draw_list, float screen_w, float screen_h) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mods_) {
            if (entry.enabled && entry.initialized) {
                entry.instance->onRender2D(draw_list, screen_w, screen_h);
            }
        }
    }

    void render3D(void* cmd_list, const void* camera_ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mods_) {
            if (entry.enabled && entry.initialized) {
                entry.instance->onRender3D(cmd_list, camera_ctx);
            }
        }
    }

    // Returns true if event was consumed by any mod, truncating downstream propagation
    bool dispatchInput(const ModInputEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mods_) {
            if (entry.enabled && entry.initialized) {
                if (entry.instance->onInput(event)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Returns true if damage was cancelled entirely
    bool filterPlayerDamage(ModDamageContext& ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mods_) {
            if (entry.enabled && entry.initialized) {
                if (entry.instance->onPlayerDamage(ctx)) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    struct ModEntry {
        std::shared_ptr<IModInstance> instance;
        bool enabled{true};
        bool initialized{false};
    };

    void sortModsLocked() {
        std::stable_sort(mods_.begin(), mods_.end(), [](const ModEntry& a, const ModEntry& b) {
            return a.instance->priority() > b.instance->priority();
        });
    }

    mutable std::mutex mutex_;
    std::vector<ModEntry> mods_;
};

} // namespace mc::contracts
