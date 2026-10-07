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

    // Where the character's eyes are: the origin of the attack/placement ray and of projectiles. Vanilla
    // Minecraft aims and measures reach from the eyes even in third person, where the camera floats behind
    // the character, so third-person hosts must override this. First-person hosts can keep the default (the
    // camera).
    virtual Vec3 getEyePosition() const { return getCameraPosition(); }
    virtual Vec3 getPlayerVelocity() const = 0;

    // The heading of the host character's own body, as a canonical yaw (0 = +X, counter-clockwise
    // positive, radians). Hosts whose character turns where it walks should report it so Steve's body
    // follows the real character; the head then turns freely up to 50 degrees off it. Return false when
    // the host has no trustworthy facing (the Session then derives the body yaw from the camera).
    virtual bool getPlayerFacingYaw(float& out_yaw) const {
        (void)out_yaw;
        return false;
    }
};

} // namespace mc
