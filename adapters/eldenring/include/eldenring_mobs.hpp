#pragma once

// The Minecraft mobs drawn over the game's summons (feat/summon). One entry per summoned character, keyed by its pointer: its own walking
// speed (from the standing points of successive frames, like the player's) and its own animator, so each zombie walks in step with the wolf
// underneath. An entry that is not seen for a while is dropped. Pure logic; the loader fills it, the overlay draws the result.

#include "eldenring_steve.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace eldenring::mobs {

inline constexpr size_t kMaxMobs = 16;
inline constexpr int kMissedFramesBeforeDrop = 30;

struct MobSnapshot {
    uintptr_t id{0};   // the character pointer
    float feet[3]{};
    float yaw{0.f};    // host rotation about +Y, the way Steve's is
};

struct MobDraw {
    uintptr_t id{0};
    eldenring::render::PartMatrices parts{};
};

class MobRegistry {
public:
    // One frame: the summons seen now. Returns what to draw (at most kMaxMobs), in the order given.
    std::vector<MobDraw> update(float dt, const std::vector<MobSnapshot>& seen) {
        for (auto& [id, e] : entries_) ++e.missed;
        std::vector<MobDraw> out;
        for (const MobSnapshot& s : seen) {
            if (s.id == 0 || out.size() >= kMaxMobs) continue;
            Entry& e = entries_[s.id];
            e.missed = 0;
            mc::SteveAnimInput in = e.motion.update(dt, s.feet, s.yaw);
            in.arms_forward = true;
            e.anim.update(dt, in);
            MobDraw d;
            d.id = s.id;
            d.parts = eldenring::render::posedMatrices(e.anim.getTransforms(), {s.feet[0], s.feet[1], s.feet[2]}, s.yaw);
            out.push_back(d);
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.missed > kMissedFramesBeforeDrop) it = entries_.erase(it);
            else ++it;
        }
        return out;
    }
    [[nodiscard]] size_t tracked() const { return entries_.size(); }
    void clear() { entries_.clear(); }

private:
    struct Entry {
        eldenring::render::SteveMotion motion;
        mc::SteveAnimator anim;
        int missed{0};
    };
    std::map<uintptr_t, Entry> entries_;
};

} // namespace eldenring::mobs
