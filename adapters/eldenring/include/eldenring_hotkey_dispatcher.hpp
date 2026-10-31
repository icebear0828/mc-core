#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <atomic>
#include <vector>

namespace eldenring::input {

struct HotkeyCallbacks {
    std::function<bool()> is_game_in_foreground;
    std::function<bool()> want_suppress;
    std::function<bool()> is_player_dead;
    std::function<bool()> is_player_airborne;
    std::function<uintptr_t()> get_player_chr;
    std::function<bool(uintptr_t, float[3])> read_physics_pos;
    std::function<void(const char*, float)> play_sound;
    std::function<void(const std::string&)> log;
    // Duration (seconds) of a clip in the active CS viewmodel, or 0 when absent.
    std::function<float(const char*)> clip_duration;
    // Duration (seconds) of an Overwatch authored clip, or 0 when absent.
    std::function<float(const char*)> ow_clip_duration;

    // DirectInput / input hook wrappers
    std::function<bool()> take_left_click;
    std::function<bool()> take_right_click;
    std::function<bool()> left_held;
    std::function<bool()> right_held;
    std::function<int()> take_wheel_notches;
    std::function<void(bool)> set_suppress_mouse_buttons;
    std::function<void(bool)> set_suppress_mouse_wheel;
    std::function<void(bool)> set_suppress_mouse_motion;
    std::function<void(bool)> set_suppress_keyboard;
    std::function<void(const int*, size_t)> set_masked_keys;
    std::function<unsigned(long&)> take_keyboard_failures;

    // Key polling override (for unit tests / mock input)
    std::function<bool(int vk)> is_key_down;

    // Gameplay Actions
    std::function<bool(float dt, bool left_held)> step_mining; // returns true if consumed
    std::function<bool()> try_ignite_tnt;
    std::function<bool()> try_ignite_creeper;
    std::function<bool()> try_place_block;
    std::function<bool()> try_use_spawn_egg;
    std::function<bool(float charged)> click_attack;
    std::function<void()> enqueue_nearest_hostile;
    std::function<bool(float damage)> queue_bullet_fire;
    // CS hitscan: damage, cone spread (degrees), max range (metres), tracer every N rounds, weapon slot.
    std::function<bool(float damage, float spread_deg, float max_range_m, uint32_t tracer_frequency, uint8_t weapon_slot)> queue_cs_shot;
    std::function<bool(const char* reason)> queue_summon;
    std::function<void(int& cursor)> cycle_hidden_slot;
    std::function<void(int& cursor)> reset_slot_probe;
    std::function<void()> snapshot_native_parts;
    std::function<void(int frames)> request_frame_trace;
    std::function<void(const char* path)> request_screenshot;
    std::function<void()> cycle_depth_candidate;
    std::function<void(bool fg)> inventory_tick;
    std::function<void()> blocks_tick;
    std::function<void()> mc_flags_step;

    // Summon watch polling
    std::function<void(uint64_t waited_ms)> check_summon_watch;
};

class HotkeyDispatcher {
public:
    static HotkeyDispatcher& Instance();

    void setCallbacks(const HotkeyCallbacks& cb);
    const HotkeyCallbacks& callbacks() const { return cb_; }

    // Single step of the key loop (takes dt in seconds)
    void tick(float dt);

    // Continuous loop for thread execution
    void runKeyLoop();

    void stop();
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

    // Internal cursor access for inspection
    int slotCursor() const { return slot_cursor_; }
    void setSlotCursor(int cursor) { slot_cursor_ = cursor; }

    // Reset internal key edge states
    void resetKeyStates();

private:
    HotkeyDispatcher();
    ~HotkeyDispatcher() = default;
    HotkeyDispatcher(const HotkeyDispatcher&) = delete;
    HotkeyDispatcher& operator=(const HotkeyDispatcher&) = delete;

    bool queryKey(int vk) const;

    HotkeyCallbacks cb_;
    std::atomic<bool> running_{false};
    int slot_cursor_{-1};

    // Edge-triggered key memory
    bool prev8_{false};
    bool prev6_{false};
    bool prev7_{false};
    bool prev_digit_[9]{};
    bool prev_space_{false};
    bool prev9_{false};
    bool prev10_{false};
    bool prev11_{false};
    bool prev12_{false};
    bool prev5_{false};
    bool prev4_{false};
    bool prev_f3_{false};
    bool prev_f2_{false};
    bool prev_f1_{false};
    bool prev_gameplay_f_{false};
    bool prev_reload_r_{false};
    bool prev_ow_e_{false};
    bool prev_ow_q_{false};
    float cs_fire_cooldown_{0.f};
    float cs_since_shot_{10.f}; // seconds since the last CS round: the first shot is accurate
};

} // namespace eldenring::input
