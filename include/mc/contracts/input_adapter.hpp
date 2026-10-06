#pragma once

#include "mc/types.hpp"

namespace mc {

class IInputAdapter {
public:
    virtual ~IInputAdapter() = default;

    // Detect currently equipped/held item in player's hands
    virtual ItemId getEquippedMainHand() const = 0;
    virtual ItemId getEquippedOffHand() const = 0;

    // Camera view ray and player state
    virtual Vec3 getCameraPosition() const = 0;
    virtual Vec3 getCameraForward() const = 0;
    virtual Vec3 getPlayerPosition() const = 0;
    virtual Vec3 getPlayerVelocity() const = 0;
};

} // namespace mc
