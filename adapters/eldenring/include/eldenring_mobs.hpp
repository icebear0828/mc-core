#pragma once

// The Minecraft mobs drawn over the game's summons (feat/summon). One entry per summoned character, keyed by its pointer: its own walking
// speed (from the standing points of successive frames, like the player's) and its own animator, so each zombie walks in step with the wolf
// underneath. An entry that is not seen for a while is dropped. Pure logic; the loader fills it, the overlay draws the result.

#include "eldenring_steve.hpp"
#include "mc/entity_model.hpp"

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
    bool generic{false};                          // true: `bones` (one matrix per bone of the model file); false: `parts` (the Steve rig)
    std::vector<mc::rig::Mat4> bones;
    eldenring::render::PartMatrices parts{};
};

class MobRegistry {
public:
    // The model file the mobs are drawn from (a humanoid whose bones are named like the Steve rig's parts). Null: the Steve rig itself.
    void setModel(const mc::model::EntityModel* model) {
        model_ = model;
        entries_.clear();
    }

    // One frame: the summons seen now. Returns what to draw (at most kMaxMobs), in the order given.
    std::vector<MobDraw> update(float dt, const std::vector<MobSnapshot>& seen) {
        for (auto& [id, e] : entries_) ++e.missed;
        std::vector<MobDraw> out;
        for (const MobSnapshot& s : seen) {
            if (s.id == 0 || out.size() >= kMaxMobs) continue;
            Entry& e = entries_[s.id];
            e.missed = 0;
            mc::SteveAnimInput in = e.motion.update(dt, s.feet, s.yaw);
            MobDraw d;
            d.id = s.id;
            if (model_ != nullptr) {
                in.arms_sway_only = true; // the file's rest rotation holds the arms out
                e.anim.update(dt, in);
                d.generic = true;
                d.bones = poseFromAnimator(*model_, e.anim.getTransforms(), s);
            } else {
                in.arms_forward = true;
                e.anim.update(dt, in);
                d.parts = eldenring::render::posedMatrices(e.anim.getTransforms(), {s.feet[0], s.feet[1], s.feet[2]}, s.yaw);
            }
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
    // One matrix per bone: the animator's rotation of the Steve part a bone is named after (bones with other names stay at rest), the figure
    // standing at the snapshot's feet and turned to its heading.
    static std::vector<mc::rig::Mat4> poseFromAnimator(const mc::model::EntityModel& model, const mc::SteveAnimator::PartTransforms& t, const MobSnapshot& s) {
        std::vector<mc::Quat> extra(model.bones.size());
        for (size_t i = 0; i < model.bones.size(); ++i) {
            if (const auto part = mc::model::humanoidPartForBone(model.bones[i].name)) extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(*part)].rot);
        }
        return mc::model::boneMatrices(model, extra, {s.feet[0], s.feet[1], s.feet[2]}, s.yaw, eldenring::render::kBasis);
    }

    const mc::model::EntityModel* model_{nullptr};
    struct Entry {
        eldenring::render::SteveMotion motion;
        mc::SteveAnimator anim;
        int missed{0};
    };
    std::map<uintptr_t, Entry> entries_;
};

} // namespace eldenring::mobs
