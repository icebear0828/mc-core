#pragma once

// Minecraft's trident: hold the right button to charge, release to throw. Pure logic (no game, no clock):
// the caller says every frame whether the button is down and whether the trident can be used right now
// (in the main hand, no screen open, alive). The charge drives the animation pose and the throw's power.

#include "mc/bow.hpp" // powerForTicks: the trident uses the same charge curve as the bow

namespace mc {

// TridentItem.releaseUsing: a throw needs 10 ticks of use (0.5 s).
inline constexpr float kTridentMinTicks = 10.f;
// TridentItem: the thrown trident's base damage is 8, independent of the charge (only the speed scales).
inline constexpr float kTridentThrowDamage = 8.f;

struct TridentUpdate {
    bool started{false};
    bool released{false};  // throw a trident with `power`
    bool cancelled{false}; // the charge ended without a throw (too short, or the trident was lost)
    float power{0.f};
};

class TridentController {
public:
    // Every frame. `usable`: a trident is in the main hand, no screen is open, the player is alive.
    TridentUpdate update(float dt, bool right_down, bool usable);

    [[nodiscard]] bool charging() const { return charging_; }
    // Ticks charged (0 when not charging): the animation pose animates with it.
    [[nodiscard]] float ticks() const { return charging_ ? ticks_ : 0.f; }
    // 0 when not charging; 0..1 while it is, for the pose, the throw and the release sound.
    [[nodiscard]] float power() const { return charging_ ? powerForTicks(ticks_) : 0.f; }
    void reset();

private:
    bool charging_{false};
    float ticks_{0.f};
    bool prev_down_{false};
};

} // namespace mc
