#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace mc {

inline constexpr float kDefaultCreeperFuseSeconds = 1.5f;   // 30 ticks
inline constexpr float kCreeperIgniteDistance = 3.5f;       // metres
inline constexpr float kCreeperDefuseDistance = 7.0f;       // metres
inline constexpr float kCreeperExplosionRadius = 7.0f;      // metres
inline constexpr float kCreeperDefaultDamage = 1500.0f;     // damage
inline constexpr float kCreeperMaxSwellScale = 1.35f;       // 35% larger at peak fuse

enum class CreeperEventKind {
    Hiss,
    Defuse,
    Explosion
};

struct CreeperEvent {
    CreeperEventKind kind{CreeperEventKind::Hiss};
    uintptr_t creeper_id{0};
    float pos[3]{};
    float radius{0.0f};
    float damage{0.0f};
    bool kill_creeper{false};
};

struct CreeperTarget {
    uintptr_t id{0};
    float feet[3]{};
    int hp{100};
    int max_hp{100};
    bool is_creeper{true};
};

struct CreeperEnemyTarget {
    uintptr_t chr{0};
    float feet[3]{};
    bool hostile{true};
    int hp{100};
};

class CreeperManager {
public:
    explicit CreeperManager(float fuse_duration = kDefaultCreeperFuseSeconds)
        : fuse_duration_(fuse_duration) {}

    bool ignite(uintptr_t id) {
        auto& state = creepers_[id];
        if (state.exploded) return false;
        if (!state.primed) {
            state.primed = true;
            state.manual = true;
            state.fuse = fuse_duration_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool isPrimed(uintptr_t id) const {
        auto it = creepers_.find(id);
        return it != creepers_.end() && it->second.primed && !it->second.exploded;
    }

    [[nodiscard]] float getSwell(uintptr_t id) const {
        auto it = creepers_.find(id);
        if (it == creepers_.end() || !it->second.primed || it->second.exploded) return 0.0f;
        const float elapsed = fuse_duration_ - it->second.fuse;
        return std::clamp(elapsed / fuse_duration_, 0.0f, 1.0f);
    }

    [[nodiscard]] float getScale(uintptr_t id) const {
        return 1.0f + (kCreeperMaxSwellScale - 1.0f) * getSwell(id);
    }

    [[nodiscard]] bool isWhiteFlash(uintptr_t id) const {
        auto it = creepers_.find(id);
        if (it == creepers_.end() || !it->second.primed || it->second.exploded) return false;
        // Period is 0.2s: white flash for 0.1s
        return std::fmod(it->second.fuse, 0.2f) < 0.1f;
    }

    std::vector<CreeperEvent> update(float dt,
                                     const std::vector<CreeperTarget>& summons,
                                     const std::vector<CreeperEnemyTarget>& enemies,
                                     float ignite_dist = kCreeperIgniteDistance,
                                     float defuse_dist = kCreeperDefuseDistance,
                                     float explosion_radius = kCreeperExplosionRadius,
                                     float explosion_damage = kCreeperDefaultDamage) {
        std::vector<CreeperEvent> events;

        for (const auto& s : summons) {
            if (!s.is_creeper || s.id == 0) continue;

            auto& state = creepers_[s.id];
            if (state.exploded) continue;

            // If dead before explosion, cancel
            if (s.max_hp > 0 && s.hp <= 0) {
                state.primed = false;
                state.fuse = fuse_duration_;
                continue;
            }

            state.last_feet[0] = s.feet[0];
            state.last_feet[1] = s.feet[1];
            state.last_feet[2] = s.feet[2];

            // Find nearest hostile enemy
            float min_dist = 1e9f;
            for (const auto& e : enemies) {
                if (!e.hostile || e.hp <= 0) continue;
                const float dx = s.feet[0] - e.feet[0];
                const float dy = s.feet[1] - e.feet[1];
                const float dz = s.feet[2] - e.feet[2];
                const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d < min_dist) min_dist = d;
            }

            if (!state.primed) {
                if (min_dist <= ignite_dist) {
                    state.primed = true;
                    state.fuse = std::max(0.0f, fuse_duration_ - dt);
                    CreeperEvent ev;
                    ev.kind = CreeperEventKind::Hiss;
                    ev.creeper_id = s.id;
                    ev.pos[0] = s.feet[0];
                    ev.pos[1] = s.feet[1];
                    ev.pos[2] = s.feet[2];
                    events.push_back(ev);
                }
            } else {
                // If not manual and enemy has moved far away, defuse
                if (!state.manual && min_dist > defuse_dist) {
                    state.primed = false;
                    state.fuse = fuse_duration_;
                    CreeperEvent ev;
                    ev.kind = CreeperEventKind::Defuse;
                    ev.creeper_id = s.id;
                    events.push_back(ev);
                    continue;
                }

                state.fuse -= dt;
                if (state.fuse <= 0.0f) {
                    state.fuse = 0.0f;
                    state.exploded = true;
                    state.primed = false;

                    CreeperEvent ev;
                    ev.kind = CreeperEventKind::Explosion;
                    ev.creeper_id = s.id;
                    ev.pos[0] = s.feet[0];
                    ev.pos[1] = s.feet[1];
                    ev.pos[2] = s.feet[2];
                    ev.radius = explosion_radius;
                    ev.damage = explosion_damage;
                    ev.kill_creeper = true;
                    events.push_back(ev);
                }
            }
        }
        return events;
    }

    void clear() {
        creepers_.clear();
    }

private:
    struct State {
        bool primed{false};
        bool manual{false};
        bool exploded{false};
        float fuse{kDefaultCreeperFuseSeconds};
        float last_feet[3]{};
    };

    float fuse_duration_{kDefaultCreeperFuseSeconds};
    std::map<uintptr_t, State> creepers_;
};

} // namespace mc
