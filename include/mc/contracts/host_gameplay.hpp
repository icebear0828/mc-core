#pragma once

// Host gameplay bindings: everything the Minecraft actions need from the game that is NOT geometry, input
// events or hit resolution (those have their own ports). Each item is something a porter has to reverse
// engineer (or find in the engine's API) once per game; docs/REVERSE_INTERFACES.md lists, per item, what it
// enables, how to find it, how to verify it and what happens without it.
//
// Every method has a harmless default, so an adapter implements only what it has found and declares that
// in supportedFeatures(). The Session degrades gracefully and reports what is still missing.

#include "mc/types.hpp"

#include <cstddef>
#include <cstdint>

namespace mc {

enum class HostFeature : uint32_t {
    PlayerVitals = 1u << 0,         // read the player's real health
    EntityEnumeration = 1u << 1,    // list nearby entities with position and health
    InputSuppression = 1u << 2,     // stop the native character reacting to attack/use input
    IncomingDamageEvents = 1u << 3, // learn about damage the player is about to take (shield, totem)
    FirstPersonCamera = 1u << 4,    // place the view at the eyes (first person), restore afterwards
    GroundedFlag = 1u << 5,         // real "on ground" instead of the vertical-speed estimate
};

inline constexpr uint32_t kAllHostFeatures = (1u << 6) - 1;

[[nodiscard]] inline const char* hostFeatureName(HostFeature f) {
    switch (f) {
        case HostFeature::PlayerVitals: return "PlayerVitals";
        case HostFeature::EntityEnumeration: return "EntityEnumeration";
        case HostFeature::InputSuppression: return "InputSuppression";
        case HostFeature::IncomingDamageEvents: return "IncomingDamageEvents";
        case HostFeature::FirstPersonCamera: return "FirstPersonCamera";
        case HostFeature::GroundedFlag: return "GroundedFlag";
    }
    return "Unknown";
}

struct HostVitals {
    float health{0.f};     // host units (any scale; only the ratio is used)
    float max_health{0.f}; // > 0 when valid
};

struct HostEntityInfo {
    EntityId id{EntityId::None};
    Vec3 position{};          // canonical MC space, centimetres, feet
    float health{0.f};
    float max_health{0.f};
    bool hostile{false};
};

struct IncomingDamage {
    EntityId attacker{EntityId::None};
    float amount{0.f};       // host health units about to be (or just) removed from the player
    float health_before{0.f};
    Vec3 direction{};        // from the attacker toward the player, canonical space
};

class IHostGameplay {
public:
    virtual ~IHostGameplay() = default;

    // Bitmask of HostFeature values this adapter really implements (and verified in the live game).
    [[nodiscard]] virtual uint32_t supportedFeatures() const { return 0; }

    // PlayerVitals. False when the value is not trustworthy this frame (loading, dead, unreadable).
    virtual bool getPlayerVitals(HostVitals& out) const {
        (void)out;
        return false;
    }

    // EntityEnumeration. Fills up to `max` entries and returns how many; ids are stable across frames
    // for the same host entity (never host pointers or handles) and >= 2 (0 and 1 are reserved).
    virtual size_t enumerateEntities(HostEntityInfo* out, size_t max) const {
        (void)out;
        (void)max;
        return 0;
    }

    // InputSuppression. While true the native character must not attack, block or use items on mouse
    // input; movement, camera and menus keep working. Idempotent, and released on setActive(false).
    virtual void setNativeCombatInputSuppressed(bool suppressed) { (void)suppressed; }

    // IncomingDamageEvents. Returns damage observed since the last call.
    virtual size_t drainIncomingDamage(IncomingDamage* out, size_t max) {
        (void)out;
        (void)max;
        return 0;
    }
    // Give health back (shield block, totem). Host units, clamped to the maximum by the host.
    virtual void refundPlayerDamage(float amount) { (void)amount; }
};

} // namespace mc
