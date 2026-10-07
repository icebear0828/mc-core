#pragma once

#include "mc/animator.hpp"
#include "mc/types.hpp"

#include <memory>

namespace mc {

class IPhysicsAdapter;
class IRenderAdapter;
class ICombatAdapter;
class IInputAdapter;
class VoxelWorld;
class CombatEngine;
class BallisticsEngine;
class ConsumableSystem;
class ElytraFlight;
class HudEngine;

// Host-side ports. All vectors crossing these ports are in canonical MC space:
// Z-up, right-handed, centimetres, yaw zero along +X.
struct Ports {
    IPhysicsAdapter& physics;
    IRenderAdapter& render;
    ICombatAdapter& combat;
    IInputAdapter& input;
};

// Per-frame input events gathered by the host loader (edge/level flags, no engine types).
struct InputSnapshot {
    bool on_ground{true};
    // True when the host cannot report real ground contact and `on_ground` is inferred (e.g. from
    // vertical speed). While gliding the Session drives the player's velocity itself, which would
    // make such an estimate read "grounded" immediately, so it is ignored mid-glide and landing is
    // left to the elytra's swept raycast.
    bool on_ground_is_estimate{false};
    bool attack_pressed{false}; // edge: left click down this frame
    bool attack_held{false};    // level: left click held (mining)
    bool use_pressed{false};    // edge: right click down this frame
    bool glide_toggle{false};   // edge: jump while airborne
    int hotbar_select{-1};      // 0..8, -1 = none
    int scroll{0};              // +1 / -1 hotbar steps
};

// Owns every rule engine and drives them from one tick. Adapters only fill
// InputSnapshot and implement the ports; they never orchestrate gameplay.
class Session {
public:
    static constexpr float kReachCm = 450.f;
    static constexpr float kSwingDurationSec = 0.3f;
    static constexpr float kAttackRechargeSec = 0.625f; // diamond sword, 1.6 attacks/s
    static constexpr float kMaxHeadYawRad = 0.8726646f; // 50 degrees: further and the body turns too
    static constexpr float kVelocitySmoothingSec = 0.12f; // host positions advance in steps; limbs must not follow the steps

    explicit Session(const Ports& ports);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void setActive(bool active);
    [[nodiscard]] bool isActive() const { return active_; }

    // Standard Minecraft hotbar (sword, pickaxe, blocks, food, bow, elytra, totem); selects slot 0.
    void loadDefaultHotbar();

    // No-op while inactive.
    void tick(float dt, const InputSnapshot& in);

    HudEngine& hud() { return *hud_; }
    VoxelWorld& voxel() { return *voxel_; }
    BallisticsEngine& ballistics() { return *ballistics_; }
    ConsumableSystem& consumables() { return *consumables_; }
    ElytraFlight& elytra() { return *elytra_; }
    [[nodiscard]] const SteveAnimInput& lastAnimInput() const { return last_anim_input_; }

private:
    void consumeSelectedOne();

    Ports ports_;
    bool active_{false};

    std::unique_ptr<SteveAnimator> animator_;
    std::unique_ptr<VoxelWorld> voxel_;
    std::unique_ptr<CombatEngine> combat_;
    std::unique_ptr<BallisticsEngine> ballistics_;
    std::unique_ptr<ConsumableSystem> consumables_;
    std::unique_ptr<ElytraFlight> elytra_;
    std::unique_ptr<HudEngine> hud_;

    SteveAnimInput last_anim_input_{};
    float body_yaw_{0.f};
    float head_side_{1.f};       // which side the head is parked on while the camera is behind the body
    Vec3 smoothed_vel_{};        // animation-only low-pass of the host velocity (cm/s)
    bool smoothed_vel_seeded_{false};
    bool body_yaw_seeded_{false}; // re-seeded from the camera each time the session is activated
    float swing_elapsed_{-1.f}; // <0 = idle
    float attack_cooldown_{1.f};
};

} // namespace mc
