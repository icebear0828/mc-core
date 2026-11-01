#include "mc/trident.hpp"

namespace mc {

TridentUpdate TridentController::update(float dt, bool right_down, bool usable) {
    TridentUpdate u;
    if (!usable) {
        if (charging_) u.cancelled = true;
        reset();
        prev_down_ = right_down;
        return u;
    }
    if (!charging_) {
        if (right_down && !prev_down_) { // only a fresh press starts a charge
            charging_ = true;
            ticks_ = 0.f;
            u.started = true;
        }
    } else {
        ticks_ += dt * kTicksPerSecond;
        if (!right_down) {
            if (ticks_ >= kTridentMinTicks) {
                u.released = true;
                u.power = powerForTicks(ticks_);
            } else {
                u.cancelled = true;
            }
            reset();
        }
    }
    prev_down_ = right_down;
    return u;
}

void TridentController::reset() {
    charging_ = false;
    ticks_ = 0.f;
}

} // namespace mc
