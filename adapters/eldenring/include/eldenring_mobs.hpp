#pragma once

// The Minecraft mobs drawn over the game's summons (feat/summon). One entry per summoned character, keyed by its pointer: its own walking
// speed (from the standing points of successive frames, like the player's) and its own animator, so each zombie walks in step with the wolf
// underneath. Its hit points give the rest of Minecraft's life cycle: a loss of hit points flashes the figure red for 10 ticks; when they run
// out it tips over onto its side (still red) and stays there for a second, even if the game has already removed the entity; an entity that
// simply disappears while alive is dropped after a while. Pure logic; the loader fills it, the overlay draws the result.

#include "eldenring_steve.hpp"
#include "mc/bow.hpp"
#include "mc/entity_model.hpp"
#include "mc/mobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace eldenring::mobs {

using mc::kMaxMobs;
using mc::kMissedFramesBeforeDrop;
using mc::kHurtSeconds;
using mc::kDeathSeconds;
using mc::deathFlipFraction;
using mc::scaleAboutPoint;
using mc::MobLifecycleTracker;

struct MobSnapshot {
    uintptr_t id{0};   // the character pointer
    float feet[3]{};
    float yaw{0.f};    // host rotation about +Y, the way Steve's is
    int hp{-1};        // the game's hit points of the character; max_hp <= 0: unknown, no hurt flash and no death scene
    int max_hp{-1};
    uint32_t npc_id{0};
    std::string model_type;
    float scale{1.0f};
    float white_flash{0.0f};
    float bow_charge{0.0f};     // [0, 1] for ranged archer aim
    float swing_progress{0.0f}; // [0, 1] for melee attack swing
};

struct MobDraw {
    uintptr_t id{0};
    float feet[3]{};   // where it stands (game metres), for its shadow
    float hurt{0.f};   // 1 just hurt .. 0 (a dying mob stays at 1): the figure is tinted red
    bool dying{false};
    float fall{0.f};   // 0..1 how far the dead body has tipped over (already applied to the matrices below)
    bool generic{false};                          // true: `bones` (one matrix per bone of the model file); false: `parts` (the Steve rig)
    std::string model_type;
    std::vector<mc::rig::Mat4> bones;
    eldenring::render::PartMatrices parts{};
    float scale{1.0f};
    float white_flash{0.0f};
    uint16_t held_item{0};
    float bow_charge{0.0f};
};

class MobRegistry {
public:
    // The model file the mobs are drawn from (a humanoid whose bones are named like the Steve rig's parts). Null: the Steve rig itself.
    void setModel(const mc::model::EntityModel* model) {
        model_ = model;
        entries_.clear();
    }

    void registerModel(std::string_view name, const mc::model::EntityModel* model) {
        if (!name.empty() && model != nullptr) {
            models_[std::string(name)] = model;
        }
    }

    [[nodiscard]] const mc::model::EntityModel* getModel(std::string_view name = "") const {
        if (!name.empty()) {
            auto it = models_.find(std::string(name));
            if (it != models_.end()) return it->second;
        }
        return model_;
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

    static inline mc::Quat mobEulerToHostQuat(float pitch, float yaw, float roll) {
        const float cy = std::cos(yaw * 0.5f);
        const float sy = std::sin(yaw * 0.5f);
        const float cp = std::cos(pitch * 0.5f);
        const float sp = std::sin(pitch * 0.5f);
        const float cr = std::cos(roll * 0.5f);
        const float sr = std::sin(roll * 0.5f);

        mc::Quat q;
        q.w = cr * cp * cy + sr * sp * sy;
        q.x = sr * cp * cy - cr * sp * sy;
        q.y = cr * sp * cy + sr * cp * sy;
        q.z = cr * cp * sy - sr * sp * cy;
        return eldenring::render::hostQuat(q);
    }

    static std::vector<mc::rig::Mat4> poseFromAnimator(const mc::model::EntityModel& model, const mc::SteveAnimator::PartTransforms& t, const MobSnapshot& s) {
        std::vector<mc::Quat> extra(model.bones.size());
        for (size_t i = 0; i < model.bones.size(); ++i) {
            const std::string& name = model.bones[i].name;
            if (s.model_type == "skeleton") {
                if (name == "right_arm") {
                    if (s.bow_charge > 0.0f) {
                        extra[i] = mobEulerToHostQuat(0.15f * s.bow_charge, -0.45f * s.bow_charge, 0.05f * s.bow_charge);
                    } else {
                        const float swing = -2.0f * (t[static_cast<size_t>(mc::StevePart::RightArm)].rot.y);
                        extra[i] = mobEulerToHostQuat(1.45f + swing, 0.0f, 0.0f);
                    }
                } else if (name == "left_arm") {
                    if (s.bow_charge > 0.0f) {
                        extra[i] = mobEulerToHostQuat(0.0f, 0.35f * s.bow_charge, -0.05f * s.bow_charge);
                    } else {
                        const float swing = -2.0f * (t[static_cast<size_t>(mc::StevePart::LeftArm)].rot.y);
                        extra[i] = mobEulerToHostQuat(1.45f + swing, 0.0f, 0.0f);
                    }
                } else if (const auto part = mc::model::humanoidPartForBone(name)) {
                    extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(*part)].rot);
                } else if (name == "leg0" || name == "leg3") {
                    extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(mc::StevePart::RightLeg)].rot);
                } else if (name == "leg1" || name == "leg2") {
                    extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(mc::StevePart::LeftLeg)].rot);
                }
            } else if (s.bow_charge > 0.0f && name == "right_arm") {
                extra[i] = mobEulerToHostQuat(0.15f * s.bow_charge, -0.45f * s.bow_charge, 0.05f * s.bow_charge);
            } else if (s.bow_charge > 0.0f && name == "left_arm") {
                extra[i] = mobEulerToHostQuat(0.0f, 0.35f * s.bow_charge, -0.05f * s.bow_charge);
            } else if (s.swing_progress > 0.0f && name == "right_arm") {
                const float swing = std::sin(s.swing_progress * 3.14159265f);
                extra[i] = mobEulerToHostQuat(-swing * 0.8f, swing * 0.3f, -swing * 0.2f);
            } else if (s.swing_progress > 0.0f && name == "left_arm") {
                const float swing = std::sin(s.swing_progress * 3.14159265f);
                extra[i] = mobEulerToHostQuat(-swing * 0.4f, -swing * 0.2f, 0.f);
            } else if (const auto part = mc::model::humanoidPartForBone(name)) {
                extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(*part)].rot);
            } else if (name == "leg0" || name == "leg3") {
                extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(mc::StevePart::RightLeg)].rot);
            } else if (name == "leg1" || name == "leg2") {
                extra[i] = eldenring::render::hostQuat(t[static_cast<size_t>(mc::StevePart::LeftLeg)].rot);
            }
        }
        return mc::model::boneMatrices(model, extra, {s.feet[0], s.feet[1], s.feet[2]}, s.yaw, eldenring::render::kBasis);
    }

    // A living mob walks; a dying one keeps the pose it died in (the animator is not advanced and the walking speed is not measured).
    MobDraw draw(uintptr_t id, Entry& e, float dt) {
        const MobSnapshot& s = e.last;
        MobDraw d;
        d.id = id;
        d.model_type = s.model_type;
        for (int i = 0; i < 3; ++i) d.feet[i] = s.feet[i];
        d.dying = e.dying >= 0.f;
        d.hurt = d.dying ? 1.f : std::min(1.f, e.hurt / kHurtSeconds);
        d.fall = d.dying ? eldenring::render::deathFlipFraction(e.dying) : 0.f;
        const mc::model::EntityModel* model = getModel(s.model_type);
        if (!d.dying) {
            mc::SteveAnimInput in = e.motion.update(dt, s.feet, s.yaw);
            in.bow_charge = s.bow_charge;
            in.swing_progress = s.swing_progress;
            if (s.model_type == "skeleton") {
                in.arms_sway_only = false;
                in.arms_forward = false;
            } else if (model != nullptr) {
                in.arms_sway_only = true; // the file's rest rotation holds the arms out
            } else {
                in.arms_forward = true;
            }
            e.anim.update(dt, in);
        }
        d.scale = s.scale > 0.01f ? s.scale : 1.0f;
        d.white_flash = s.white_flash;
        if (s.model_type == "skeleton") {
            d.held_item = static_cast<uint16_t>(mc::shownBow(mc::ItemId::Bow, s.bow_charge));
            d.bow_charge = s.bow_charge;
        }
        const mc::Vec3 feet{s.feet[0], s.feet[1], s.feet[2]};
        const mc::rig::Mat4 flip = d.fall > 0.f ? eldenring::render::deathFallMatrix(feet, s.yaw, d.fall) : mc::rig::Mat4{};
        const mc::rig::Mat4 swell_mat = d.scale != 1.0f ? scaleAboutPoint(feet, d.scale) : mc::rig::Mat4{};
        if (model != nullptr) {
            d.generic = true;
            d.bones = poseFromAnimator(*model, e.anim.getTransforms(), s);
            for (auto& m : d.bones) {
                if (d.scale != 1.0f) m = m * swell_mat;
                m = m * flip;
            }
        } else {
            d.parts = eldenring::render::posedMatrices(e.anim.getTransforms(), feet, s.yaw);
            for (auto& m : d.parts) {
                if (d.scale != 1.0f) m = m * swell_mat;
                m = m * flip;
            }
        }
        return d;
    }

    const mc::model::EntityModel* model_{nullptr};
    std::map<std::string, const mc::model::EntityModel*> models_;
    std::map<uintptr_t, Entry> entries_;
};

} // namespace eldenring::mobs
