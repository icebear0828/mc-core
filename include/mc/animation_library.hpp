#pragma once

#include "mc/animator.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mc::anim {

// A single 3D vector keyframe (e.g., Euler rotation in degrees or translation in pixels)
struct VectorKeyframe {
    float time{0.0f};           // Time in seconds
    Vec3 value{0.0f, 0.0f, 0.0f}; // Value (rotation degrees or position offset)
    std::string interpolation{"linear"}; // "linear", "catmullrom", "step"
};

// Keyframe track for a single bone
struct BoneAnimationTrack {
    std::string bone_name;
    std::vector<VectorKeyframe> position_frames;
    std::vector<VectorKeyframe> rotation_frames; // in degrees
    std::vector<VectorKeyframe> scale_frames;
};

// A named animation clip (e.g., "walk", "idle", "jump", "sprint")
struct AnimationClip {
    std::string name;
    float length{0.0f};       // Duration in seconds
    bool loop{true};
    bool hold_on_last_frame{false};
    std::vector<BoneAnimationTrack> tracks;

    [[nodiscard]] const BoneAnimationTrack* findTrack(std::string_view bone_name) const {
        for (const auto& t : tracks) {
            if (t.bone_name == bone_name) return &t;
        }
        return nullptr;
    }
};

// Interpolation helper for keyframe samples
Vec3 sampleKeyframes(const std::vector<VectorKeyframe>& frames, float time, const Vec3& default_val = {0.0f, 0.0f, 0.0f});

// Evaluated bone pose at a given time
struct EvaluatedBonePose {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

// A collection of animation clips loaded from a .bbmodel or Bedrock animation file
class AnimationLibrary {
public:
    void addClip(AnimationClip clip) {
        clips_[clip.name] = std::move(clip);
    }

    [[nodiscard]] const AnimationClip* findClip(std::string_view name) const {
        const auto it = clips_.find(std::string(name));
        return it != clips_.end() ? &it->second : nullptr;
    }

    [[nodiscard]] bool hasClip(std::string_view name) const {
        return clips_.contains(std::string(name));
    }

    [[nodiscard]] size_t clipCount() const { return clips_.size(); }

    [[nodiscard]] std::vector<std::string> clipNames() const {
        std::vector<std::string> names;
        names.reserve(clips_.size());
        for (const auto& [name, _] : clips_) names.push_back(name);
        return names;
    }

    // Parse all animations from a Blockbench .bbmodel JSON string
    static std::optional<AnimationLibrary> fromBbmodelJson(std::string_view json_str);

private:
    std::unordered_map<std::string, AnimationClip> clips_;
};

// =========================================================================================
// Data-Driven Clip Player & State Machine
// =========================================================================================

class ClipPlayer {
public:
    explicit ClipPlayer(const AnimationLibrary& library) : library_(&library) {}

    // Play a clip by name with optional cross-fade blend time
    bool play(std::string_view clip_name, float blend_time = 0.1f, bool loop = true);

    // Update playhead by dt seconds
    void update(float dt);

    // Sample evaluated pose for a specific bone
    [[nodiscard]] EvaluatedBonePose sampleBone(std::string_view bone_name) const;

    // Sample all StevePart transforms according to StevePart enum
    [[nodiscard]] SteveAnimator::PartTransforms sampleSteveParts() const;

    [[nodiscard]] std::string_view currentClip() const { return current_clip_name_; }
    [[nodiscard]] float currentTime() const { return current_time_; }
    [[nodiscard]] bool isPlaying() const { return current_clip_ != nullptr; }

private:
    const AnimationLibrary* library_{nullptr};
    const AnimationClip* current_clip_{nullptr};
    const AnimationClip* prev_clip_{nullptr};
    std::string current_clip_name_;
    float current_time_{0.0f};
    float prev_time_{0.0f};
    float blend_duration_{0.0f};
    float blend_elapsed_{0.0f};
    bool loop_{true};
};

// =========================================================================================
// High-Level Steve Animation State Machine (Driven by Gameplay State & .bbmodel Clips)
// =========================================================================================

class SteveAnimationStateMachine {
public:
    explicit SteveAnimationStateMachine(const AnimationLibrary& library) : player_(library) {
        player_.play("idle", 0.0f, true);
    }

    // Updates state machine according to input physics & actions, transitions clips smoothly
    void update(float dt, const SteveAnimInput& input);

    [[nodiscard]] SteveAnimator::PartTransforms getTransforms() const;

    [[nodiscard]] std::string_view activeClip() const { return player_.currentClip(); }

private:
    ClipPlayer player_;
    SteveAnimInput last_input_{};
};

} // namespace mc::anim

