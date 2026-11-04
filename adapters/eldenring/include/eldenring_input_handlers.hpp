#pragma once

// Domain handlers for the hotkey dispatcher. Each one owns a single gameplay
// area (CS gunplay, OW hero skills, MC interaction, MC movement, debug tuning)
// so the coordinator only builds the per-tick context and calls them in order.
//
// HotkeyCallbacks is defined in eldenring_hotkey_dispatcher.hpp; the handlers
// only pass it around, so a forward declaration keeps this header light.

#include <cstdint>

#include "eldenring_audio_data.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_shield.hpp"

namespace eldenring::input {

struct HotkeyCallbacks;

// Everything a handler needs for one tick. Built once by HotkeyDispatcher::tick
// so every handler sees the same mode snapshot and key edges.
struct InputTickContext {
    float dt{0.f};
    bool fg{true};
    bool want_suppress{false};
    bool is_dead{false};
    bool inv_open{false};
    bool inv_open_policy{false};
    bool mc_on{false};
    bool input_enabled{false};
    bool click_edge{false};
    bool left_held{false};
    bool right_edge{false};
    bool want_suppress_click{false};
    bool cs_gun_equipped{false};
    bool cs_knife_owns_melee{false};
    bool mc_melee_owner{false};
    const HotkeyCallbacks* cb{nullptr};
    int* slot_cursor{nullptr};
};

class IInputHandler {
public:
    virtual ~IInputHandler() = default;
    virtual void reset() = 0;
};

class CsGunplayHandler final : public IInputHandler {
public:
    void reset() override;

    // Returns true when the right-click belongs to CS (the knife's heavy attack).
    bool tickRightClick(const InputTickContext& ctx);
    void tickActionTimer(const InputTickContext& ctx);
    void tickGameplayKeys(const InputTickContext& ctx);
    void tickAttack(const InputTickContext& ctx);

private:
    void selectWeaponSlot(uint8_t slot, const InputTickContext& ctx);
    void tickEconomy(const InputTickContext& ctx);
    void buyMagazine(uint8_t buy_key, const InputTickContext& ctx);
    void syncSlotReserve(uint8_t slot);

    float cs_fire_cooldown_{0.f};
    float cs_since_shot_{10.f}; // seconds since the last CS round: the first shot is accurate
    int cs_reload_sound_stage_{0}; // next generated reload sound event to play
    int cs_fire_sound_stage_{0};   // next generated fire-cycle sound event to play
    bool prev_reload_r_{false};
    bool prev_gameplay_f_{false};
    bool prev_q_{false};
    bool prev_buy_b_{false};
    uint8_t last_slot_{0}; // CS2-style Q target: the weapon held before the current one
    // Per-slot magazines so a switch does not refill ammo.
    uint32_t slot_ammo_[4]{};
    uint32_t slot_reserve_[4]{};
    bool slot_ammo_ready_[4]{};
    bool prev_digit_[9]{};
};

class OwHeroSkillHandler final : public IInputHandler {
public:
    void reset() override;

    // Returns true when the right-click belongs to OW (rocket punch charge).
    bool tickRightClick(const InputTickContext& ctx);
    void tickActionTimer(const InputTickContext& ctx);
    void tickAttack(const InputTickContext& ctx);

    void stopChargeLoop(const HotkeyCallbacks& cb);
    void resetRuntimeForEnable();

private:
    float ow_fire_cooldown_{0.f};
    float ow_punch_cooldown_left_{0.f};
    float ow_uppercut_cooldown_left_{0.f};
    float ow_slam_cooldown_left_{0.f};
    float ow_hitstop_left_{0.f};
    float ow_since_shot_{10.f};
    bool ow_slam_impact_played_{false};
    bool ow_charge_looping_{false};
    bool prev_ow_e_{false};
    bool prev_ow_q_{false};
    bool ow_punch_released_{false}; // RMB hold-to-charge: true once the punch fired
};

class McInteractionHandler final : public IInputHandler {
public:
    void reset() override;

    void tickRightClick(const InputTickContext& ctx);
    void tickBow(const InputTickContext& ctx);
    void tickTrident(const InputTickContext& ctx);
    void tickShield(const InputTickContext& ctx);
    void tickWheel(const InputTickContext& ctx);
    void tickGameplayKeys(const InputTickContext& ctx);
    void tickMelee(const InputTickContext& ctx);

private:
    bool prev_gameplay_f_{false};
    bool prev_digit_[9]{};
    float place_repeat_sec_{0.f}; // hold-to-place cadence, reset on the click edge
    bool place_repeat_armed_{false}; // only a fresh right-click edge starts the cadence
    bow::BowController bow_;
    shield::ShieldController shield_;
    float trident_charge_ticks_{0.f};
    bool trident_charging_{false};
};

class McMovementHandler final : public IInputHandler {
public:
    void reset() override;

    void tickSprint(const InputTickContext& ctx);
    void tickFootsteps(const InputTickContext& ctx);
    void tickJump(const InputTickContext& ctx);

private:
    bool prev_space_{false};
    float prev_pos_[3]{};
    bool have_prev_{false};
    audio::StepClock steps_;
};

class DebugTuningHandler final : public IInputHandler {
public:
    explicit DebugTuningHandler(OwHeroSkillHandler& ow);
    void reset() override;

    // F1/F2/F3, screenshots, recording, CS/OW view tuning and mode toggles.
    void tick(const InputTickContext& ctx);

private:
    OwHeroSkillHandler& ow_;
    unsigned shot_no_{0};
    bool prev8_{false};
    bool prev6_{false};
    bool prev7_{false};
    bool prev9_{false};
    bool prev10_{false};
    bool prev11_{false};
    bool prev12_{false};
    bool prev5_{false};
    bool prev4_{false};
    bool prev_f3_{false};
    bool prev_f2_{false};
    bool prev_f1_{false};
    bool prev_csview_keys_[8]{};  // [ ] , . ; ' - = debug tuning keys
    bool prev_owview_keys_[10]{}; // [ ] , . ; ' - = plus \ / for scale
    bool prev_owvfx_keys_[2]{};   // PageUp depth toggle, PageDown scale cycle
};

} // namespace eldenring::input
