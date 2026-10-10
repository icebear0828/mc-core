#pragma once

// The Minecraft mobs drawn over the game's summons (feat/summon). One entry per summoned character, keyed by its pointer: its own walking
// speed (from the standing points of successive frames, like the player's) and its own animator, so each zombie walks in step with the wolf
// underneath. Its hit points give the rest of Minecraft's life cycle: a loss of hit points flashes the figure red for 10 ticks; when they run
// out it tips over onto its side (still red) and stays there for a second, even if the game has already removed the entity; an entity that
// simply disappears while alive is dropped after a while. Pure logic; the loader fills it, the overlay draws the result.

#include "eldenring_steve.hpp"
#include "mc/entity_model.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace eldenring::mobs {

inline constexpr size_t kMaxMobs = 16;
inline constexpr int kMissedFramesBeforeDrop = 30;
inline constexpr float kHurtSeconds = 0.5f;  // Minecraft: 10 ticks of hurt time (the red flash)
inline constexpr float kDeathSeconds = 1.0f; // the body lies there for a moment (20 ticks) before the mob is removed

struct MobSnapshot {
    uintptr_t id{0};   // the character pointer
    float feet[3]{};
    float yaw{0.f};    // host rotation about +Y, the way Steve's is
    int hp{-1};        // the game's hit points of the character; max_hp <= 0: unknown, no hurt flash and no death scene
    int max_hp{-1};
};

struct MobDraw {
    uintptr_t id{0};
    float feet[3]{};   // where it stands (game metres), for its shadow
    float hurt{0.f};   // 1 just hurt .. 0 (a dying mob stays at 1): the figure is tinted red
    bool dying{false};
    float fall{0.f};   // 0..1 how far the dead body has tipped over (already applied to the matrices below)
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

    // One frame: the summons seen now. Returns what to draw (at most kMaxMobs): the ones seen, then the ones that have just died and have
    // already been removed by the game (their body stays where it fell for the rest of kDeathSeconds).
    std::vector<MobDraw> update(float dt, const std::vector<MobSnapshot>& seen) {
        for (auto& [id, e] : entries_) ++e.missed;
        std::vector<MobDraw> out;
        for (const MobSnapshot& s : seen) {
            if (s.id == 0 || out.size() >= kMaxMobs) continue;
            Entry& e = entries_[s.id];
            e.missed = 0;
            e.last = s;
            bool just_died = false;
            bool just_hurt = false;
            if (s.max_hp > 0) {
                if (e.have_hp && s.hp < e.last_hp && s.hp > 0) just_hurt = true;
                if (s.hp <= 0 && e.dying < 0.f) {
                    e.dying = 0.f;
                    just_died = true;
                }
                e.last_hp = s.hp;
                e.have_hp = true;
            }
            if (just_hurt) e.hurt = kHurtSeconds;
            else if (!just_died) e.hurt = std::max(0.f, e.hurt - dt);
            if (e.dying >= 0.f && !just_died) e.dying += dt;
            if (e.dying > kDeathSeconds) continue; // the game may keep the corpse for seconds; the body is gone after kDeathSeconds anyway
            out.push_back(draw(s.id, e, dt));
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            Entry& e = it->second;
            if (e.missed > 0 && e.dying >= 0.f) { // died, then the game removed it: the body stays for the rest of its death time
                e.dying += dt;
                if (e.dying > kDeathSeconds) {
                    it = entries_.erase(it);
                    continue;
                }
                if (out.size() < kMaxMobs) out.push_back(draw(it->first, e, dt));
            } else if (e.missed > kMissedFramesBeforeDrop) {
                it = entries_.erase(it);
                continue;
            }
            ++it;
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
        MobSnapshot last{};
        int last_hp{0};
        bool have_hp{false};
        float hurt{0.f};   // seconds of red flash left
        float dying{-1.f}; // seconds since the hit points ran out; < 0: alive
    };

    // One matrix per bone: the animator's rotation of the Steve part a bone is named after (bones with other names stay at rest), the figure
    // standing at the snapshot's feet and turned to its heading.
    static std::vector<mc::rig::Mat4> poseFromAnimator(const mc::model::EntityModel& model, const mc::SteveAnimator::PartTransforms& t, const MobSnapshot& s) {
        std::vector<mc::Quat> extra(model.bones.size());
        for (size_t i = 0; i < model.bones.size(); ++i) {
            if (const auto part = mc::model::humanoidPartForBone(model.bones[i].name)) extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(*part)].rot);
        }
        return mc::model::boneMatrices(model, extra, {s.feet[0], s.feet[1], s.feet[2]}, s.yaw, eldenring::render::kBasis);
    }

    // A living mob walks; a dying one keeps the pose it died in (the animator is not advanced and the walking speed is not measured).
    MobDraw draw(uintptr_t id, Entry& e, float dt) {
        const MobSnapshot& s = e.last;
        MobDraw d;
        d.id = id;
        for (int i = 0; i < 3; ++i) d.feet[i] = s.feet[i];
        d.dying = e.dying >= 0.f;
        d.hurt = d.dying ? 1.f : std::min(1.f, e.hurt / kHurtSeconds);
        d.fall = d.dying ? eldenring::render::deathFlipFraction(e.dying) : 0.f;
        if (!d.dying) {
            mc::SteveAnimInput in = e.motion.update(dt, s.feet, s.yaw);
            if (model_ != nullptr) in.arms_sway_only = true; // the file's rest rotation holds the arms out
            else in.arms_forward = true;
            e.anim.update(dt, in);
        }
        const mc::Vec3 feet{s.feet[0], s.feet[1], s.feet[2]};
        const mc::rig::Mat4 flip = d.fall > 0.f ? eldenring::render::deathFallMatrix(feet, s.yaw, d.fall) : mc::rig::Mat4{};
        if (model_ != nullptr) {
            d.generic = true;
            d.bones = poseFromAnimator(*model_, e.anim.getTransforms(), s);
            for (auto& m : d.bones) m = m * flip;
        } else {
            d.parts = eldenring::render::posedMatrices(e.anim.getTransforms(), feet, s.yaw);
            for (auto& m : d.parts) m = m * flip;
        }
        return d;
    }

    const mc::model::EntityModel* model_{nullptr};
    std::map<uintptr_t, Entry> entries_;
};

} // namespace eldenring::mobs
