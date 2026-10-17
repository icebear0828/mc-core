#pragma once

#include <cstdint>
#include <string>

namespace mc::contracts {

struct ModInputEvent {
    uint32_t key_code{0};
    bool is_down{false};
    bool is_repeat{false};
    float mouse_x{0.0f};
    float mouse_y{0.0f};
};

struct ModDamageContext {
    float damage{0.0f};
    bool is_critical{false};
    uintptr_t attacker_id{0};
    uintptr_t victim_id{0};
};

struct CoreHostServices {
    void* user_data{nullptr};
};

class IModInstance {
public:
    virtual ~IModInstance() = default;

    // Unique mod identifier, e.g. "minecraft", "counter_strike"
    virtual const char* name() const = 0;

    // Higher values execute first in priority ordering
    virtual int priority() const { return 0; }

    // Lifecycle
    virtual bool onInitialize(CoreHostServices& services) = 0;
    virtual void onShutdown() = 0;

    // Execution & Ticking
    virtual void onTick(float dt) = 0;

    // Render pipeline dispatch
    virtual void onRender2D(void* draw_list, float screen_w, float screen_h) = 0;
    virtual void onRender3D(void* cmd_list, const void* camera_ctx) = 0;

    // Input interception. Return true if consumed (truncates propagation to lower-priority mods)
    virtual bool onInput(const ModInputEvent& event) = 0;

    // Player damage pipeline. Return true to cancel damage entirely
    virtual bool onPlayerDamage(ModDamageContext& ctx) = 0;
};

} // namespace mc::contracts
