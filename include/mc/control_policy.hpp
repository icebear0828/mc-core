#pragma once

// Control policy: one game-agnostic place to decide who owns the player's input,
// camera, movement and actions for the current mode.
//
// A feature (Minecraft mode, Counter-Strike mode, first person, inventory, flight,
// ...) submits ControlRequests. ControlResolver merges them by priority into one
// ControlPlan. The adapter applies the plan through its host input layer; it does
// not invent another `if (mode)` chain.
//
// Key ids in ControlRequest::key are host-defined (a DirectInput scan code, a
// virtual key, ...). The core never interprets them; it only deduplicates and
// carries them into ControlPlan::masked_keys.
//
// Pure logic: no platform headers, no allocation, unit-tested.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mc {

enum class ControlDomain : uint8_t {
    MouseButtons, // native attack/block/use on mouse buttons
    MouseWheel,   // native quick-slot / zoom wheel
    MouseMotion,  // native camera or cursor from mouse movement
    Keyboard,     // the whole keyboard
    Key,          // one key: ControlRequest::key > 0
    Camera,       // who writes the camera (first person, cinematic, ...)
    Movement,     // who drives horizontal movement
    Jump,         // who drives the jump / vertical impulse
    Attack,       // attack actions (not the raw mouse channel)
    Use,          // use / block / eat actions
    Roll,         // dodge roll
    Sprint,       // sprint
    Sneak,        // sneak / crouch
    Menu,         // game menu input
    Hotbar,       // hotbar / quick-slot selection
    Cursor,       // virtual cursor (inventory UI)
    Count
};

inline constexpr size_t kControlDomainCount = static_cast<size_t>(ControlDomain::Count);
static_assert(kControlDomainCount <= 32, "packed authorities use two bits per domain");

enum class ControlAuthority : uint8_t {
    Native, // the host game owns this domain
    Shared, // both the host and the mod may act
    Mod,    // the mod owns this domain; the host must not act
    None    // nobody acts (frozen)
};

[[nodiscard]] inline const char* controlDomainName(ControlDomain domain) {
    switch (domain) {
        case ControlDomain::MouseButtons: return "MouseButtons";
        case ControlDomain::MouseWheel: return "MouseWheel";
        case ControlDomain::MouseMotion: return "MouseMotion";
        case ControlDomain::Keyboard: return "Keyboard";
        case ControlDomain::Key: return "Key";
        case ControlDomain::Camera: return "Camera";
        case ControlDomain::Movement: return "Movement";
        case ControlDomain::Jump: return "Jump";
        case ControlDomain::Attack: return "Attack";
        case ControlDomain::Use: return "Use";
        case ControlDomain::Roll: return "Roll";
        case ControlDomain::Sprint: return "Sprint";
        case ControlDomain::Sneak: return "Sneak";
        case ControlDomain::Menu: return "Menu";
        case ControlDomain::Hotbar: return "Hotbar";
        case ControlDomain::Cursor: return "Cursor";
        case ControlDomain::Count: break;
    }
    return "Unknown";
}

[[nodiscard]] inline const char* controlAuthorityName(ControlAuthority authority) {
    switch (authority) {
        case ControlAuthority::Native: return "Native";
        case ControlAuthority::Shared: return "Shared";
        case ControlAuthority::Mod: return "Mod";
        case ControlAuthority::None: return "None";
    }
    return "Unknown";
}

inline constexpr size_t kMaxControlRequests = 32;
inline constexpr size_t kMaxMaskedKeys = 32;

struct ControlRequest {
    ControlDomain domain{ControlDomain::Movement};
    ControlAuthority authority{ControlAuthority::Native};
    // Higher priority wins. Equal priority: the request registered later wins.
    int priority{0};
    // Only read when domain == ControlDomain::Key; <= 0 means "no key".
    int key{0};
    // Optional diagnostic label, e.g. "first person" or "cs mode".
    const char* reason{nullptr};
};

struct ControlPlan {
    std::array<ControlAuthority, kControlDomainCount> authority{};
    std::array<int, kMaxMaskedKeys> masked_keys{};
    size_t masked_key_count{0};

    [[nodiscard]] ControlAuthority authorityFor(ControlDomain domain) const {
        const size_t index = static_cast<size_t>(domain);
        return index < kControlDomainCount ? authority[index] : ControlAuthority::Native;
    }

    // True when the host must not act: the mod owns the domain or it is frozen.
    [[nodiscard]] bool blocksNative(ControlDomain domain) const {
        const ControlAuthority value = authorityFor(domain);
        return value == ControlAuthority::Mod || value == ControlAuthority::None;
    }

    // True only when the mod actively owns the domain (not merely frozen).
    [[nodiscard]] bool owns(ControlDomain domain) const {
        return authorityFor(domain) == ControlAuthority::Mod;
    }

    [[nodiscard]] bool isDefault() const {
        for (const ControlAuthority value : authority) {
            if (value != ControlAuthority::Native) return false;
        }
        return masked_key_count == 0;
    }

    // Two bits per domain, suitable for a lock-free atomic in the adapter.
    [[nodiscard]] uint64_t packedAuthorities() const {
        uint64_t bits = 0;
        for (size_t i = 0; i < kControlDomainCount; ++i) {
            const unsigned shift = static_cast<unsigned>(i) * 2u;
            bits |= (static_cast<uint64_t>(authority[i]) & 0x3u) << shift;
        }
        return bits;
    }
};

[[nodiscard]] inline ControlAuthority controlAuthorityFromPacked(uint64_t bits, ControlDomain domain) {
    const size_t index = static_cast<size_t>(domain);
    if (index >= kControlDomainCount) return ControlAuthority::Native;
    const unsigned shift = static_cast<unsigned>(index) * 2u;
    return static_cast<ControlAuthority>((bits >> shift) & 0x3u);
}

class ControlResolver {
public:
    void clear() { count_ = 0; }

    void request(const ControlRequest& request) {
        if (count_ < kMaxControlRequests) requests_[count_++] = request;
    }

    void request(ControlDomain domain, ControlAuthority authority, int priority = 0, int key = 0,
                 const char* reason = nullptr) {
        request(ControlRequest{domain, authority, priority, key, reason});
    }

    [[nodiscard]] size_t requestCount() const { return count_; }

    [[nodiscard]] ControlPlan resolve() const {
        ControlPlan plan;
        std::array<int, kControlDomainCount> domain_priority{};
        domain_priority.fill(std::numeric_limits<int>::min());

        struct KeyEntry {
            int key{0};
            ControlAuthority authority{ControlAuthority::Native};
            int priority{0};
        };
        std::array<KeyEntry, kMaxMaskedKeys> keys{};
        size_t key_count = 0;

        for (size_t i = 0; i < count_; ++i) {
            const ControlRequest& request = requests_[i];

            if (request.domain == ControlDomain::Key) {
                if (request.key <= 0) continue;

                size_t found = key_count;
                for (size_t k = 0; k < key_count; ++k) {
                    if (keys[k].key == request.key) {
                        found = k;
                        break;
                    }
                }
                if (found == key_count) {
                    if (key_count >= kMaxMaskedKeys) continue;
                    keys[key_count++] = KeyEntry{request.key, request.authority, request.priority};
                } else if (request.priority >= keys[found].priority) {
                    keys[found].authority = request.authority;
                    keys[found].priority = request.priority;
                }
                continue;
            }

            const size_t index = static_cast<size_t>(request.domain);
            if (index >= kControlDomainCount) continue;
            if (request.priority >= domain_priority[index]) {
                domain_priority[index] = request.priority;
                plan.authority[index] = request.authority;
            }
        }

        for (size_t k = 0; k < key_count; ++k) {
            if (keys[k].authority == ControlAuthority::Mod || keys[k].authority == ControlAuthority::None) {
                plan.masked_keys[plan.masked_key_count++] = keys[k].key;
            }
        }
        return plan;
    }

private:
    std::array<ControlRequest, kMaxControlRequests> requests_{};
    size_t count_{0};
};

// ---- Common feature policies -------------------------------------------------
// These are deliberately small and composable. A game adapter adds its own
// requests for modes the core does not know about.

// The mod owns combat: native mouse buttons, attack and use must stay quiet.
inline void requestMinecraftCombat(ControlResolver& resolver, int priority = 100) {
    resolver.request(ControlDomain::MouseButtons, ControlAuthority::Mod, priority, 0, "mc combat");
    resolver.request(ControlDomain::Attack, ControlAuthority::Mod, priority, 0, "mc combat");
    resolver.request(ControlDomain::Use, ControlAuthority::Mod, priority, 0, "mc combat");
}

// First person: the mod writes the camera. Movement stays shared because the host
// still resolves collision and the mod writes the desired velocity.
inline void requestFirstPerson(ControlResolver& resolver, int priority = 100) {
    resolver.request(ControlDomain::Camera, ControlAuthority::Mod, priority, 0, "first person");
    resolver.request(ControlDomain::Movement, ControlAuthority::Shared, priority, 0, "first person");
}

// Inventory UI: the game must not move the camera, walk or open menus.
inline void requestInventory(ControlResolver& resolver, int priority = 100) {
    resolver.request(ControlDomain::MouseMotion, ControlAuthority::Mod, priority, 0, "inventory");
    resolver.request(ControlDomain::Keyboard, ControlAuthority::Mod, priority, 0, "inventory");
    resolver.request(ControlDomain::Cursor, ControlAuthority::Mod, priority, 0, "inventory");
}

} // namespace mc
