#pragma once

// Game-neutral first-person camera policy, shared by every adapter.
//
// The host-specific work (which pointer to write, which engine call hides the player model) stays behind
// IFirstPersonCamera; this class owns the parts every port must get right:
//   - the eye sits at the feet plus the eye height,
//   - the eye height eases between standing and sneaking, frame-rate independently,
//   - enter saves the native camera once, exit restores it once, in the reverse order of the writes,
//   - the FOV is written only while active and the native value comes back with exit().
//
// Enter is deliberately a scoped operation: an adapter that copies the view for one frame calls enter() before
// its own copy and exit() after it, which keeps the game's camera untouched outside that frame.

#include "mc/contracts/first_person_camera.hpp"

#include <cmath>

namespace mc::rig {

class FirstPersonRig {
public:
    static constexpr float kStandingEyeHeight = 1.65f;     // metres
    static constexpr float kSneakingEyeHeight = 1.27f;     // metres
    static constexpr float kEyeHeightTimeConstant = 0.05f; // seconds (~95% of the way in 150 ms)

    explicit FirstPersonRig(IFirstPersonCamera& camera) noexcept : camera_(&camera) {}

    // Feet (world metres, Y up) + eye height -> the camera position at the eyes.
    static void eyeFromFeet(const float feet[3], float eye_height, float out[3]) noexcept {
        out[0] = feet[0];
        out[1] = feet[1] + eye_height;
        out[2] = feet[2];
    }

    // Frame-rate independent exponential interpolation towards the target eye height. Returns
    // `current_eye_height` unchanged when first person is off or `dt` is not positive.
    static float stepSneakEyeHeight(float current_eye_height, bool sneaking, bool first_person, float dt) noexcept {
        if (!first_person || !(dt > 0.f)) return current_eye_height;
        const float target = sneaking ? kSneakingEyeHeight : kStandingEyeHeight;
        const float blend = 1.f - std::exp(-dt / kEyeHeightTimeConstant);
        return current_eye_height + (target - current_eye_height) * blend;
    }

    // Turn first person on: hide the native head, save the native camera, then write the eye once. Returns false
    // and stays inactive when the host camera cannot be saved, so nothing is written in that case.
    bool enter(const float feet[3]) {
        if (active_) return true;
        if (!camera_->save()) return false;
        camera_->hideNativeHead(true);
        hide_head_ = true;
        active_ = true;
        return writeEye(feet);
    }

    // Turn first person off: put the native camera back, then show the native head again (the reverse of
    // enter). Safe to call when inactive.
    void exit() {
        if (!active_) return;
        camera_->restore();
        if (hide_head_) {
            camera_->hideNativeHead(false);
            hide_head_ = false;
        }
        fov_written_ = false;
        active_ = false;
    }

    // Write the eye for the current frame. No-op (and false) while inactive.
    bool writeEye(const float feet[3]) {
        if (!active_) return false;
        float eye[3];
        eyeFromFeet(feet, eye_height_, eye);
        return camera_->writeEye(eye);
    }

    // FOV policy: write the requested vertical FOV (radians) only while active; the native value is put back by
    // exit(). A non-positive FOV is ignored.
    bool writeFov(float fov_y) {
        if (!active_ || !(fov_y > 0.f)) return false;
        if (!camera_->writeFov(fov_y)) return false;
        fov_written_ = true;
        return true;
    }

    // Advance the sneak eye-height smoothing. Only moves while active (first person on).
    float stepEyeHeight(bool sneaking, float dt) noexcept {
        eye_height_ = stepSneakEyeHeight(eye_height_, sneaking, active_, dt);
        return eye_height_;
    }

    void setEyeHeight(float eye_height) noexcept { eye_height_ = eye_height; }
    [[nodiscard]] float eyeHeight() const noexcept { return eye_height_; }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool hideHead() const noexcept { return hide_head_; }
    [[nodiscard]] bool fovWritten() const noexcept { return fov_written_; }

private:
    IFirstPersonCamera* camera_;
    bool active_{false};
    bool hide_head_{false};
    bool fov_written_{false};
    float eye_height_{kStandingEyeHeight};
};

} // namespace mc::rig
