#include "mc/animator.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace mc {

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDeg = kPi / 180.0f;
constexpr float kCmPerModelPixel = 5.625f; // 180 cm / 32 model pixels (matches mc::rig::kCmPerModelPixel)

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Conversion from Euler angles (pitch, yaw, roll in radians) to Quat
Quat fromEuler(float pitch, float yaw, float roll) {
    const float cy = std::cos(yaw * 0.5f);
    const float sy = std::sin(yaw * 0.5f);
    const float cp = std::cos(pitch * 0.5f);
    const float sp = std::sin(pitch * 0.5f);
    const float cr = std::cos(roll * 0.5f);
    const float sr = std::sin(roll * 0.5f);

    Quat q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    return q;
}

// Whole-body rotations follow the Blockbench app's Three.js Euler 'XYZ' order
// (R = Rx * Ry * Rz), which in the animator's canonical axes is q_pitch * q_yaw * q_roll.
// Only the sign of the pitch*yaw cross term differs from fromEuler() above.
Quat fromEulerXyz(float pitch, float yaw, float roll) {
    const float cp = std::cos(pitch * 0.5f);
    const float sp = std::sin(pitch * 0.5f);
    const float cy = std::cos(yaw * 0.5f);
    const float sy = std::sin(yaw * 0.5f);
    const float cr = std::cos(roll * 0.5f);
    const float sr = std::sin(roll * 0.5f);

    Quat q;
    q.x = cp * cy * sr + sp * sy * cr;
    q.y = sp * cy * cr + cp * sy * sr;
    q.z = cp * sy * cr - sp * cy * sr;
    q.w = cp * cy * cr - sp * sy * sr;
    return q;
}
} // namespace

SteveAnimator::SteveAnimator() {
    reset();
}

void SteveAnimator::reset() {
    age_ = 0.f;
    stride_ = 0.f;
    glide_age_ = 0.f;
    root_ = Transform{};
    upper_ = Transform{};
    for (auto& t : transforms_) {
        t = Transform{};
    }
}

void SteveAnimator::update(float dt, const SteveAnimInput& input) {
    age_ += dt * 20.0f; // 20 MC ticks per second

    const float speed = std::sqrt(input.forward_speed * input.forward_speed +
                                  input.strafe_speed * input.strafe_speed);
    float max_amp = input.is_sprinting ? 1.25f : 1.0f;
    if (input.is_crouching) {
        max_amp *= 0.6f; // Dampen sneak walking stride to avoid unnatural large steps
    }
    const float raw_amp = (speed / 4.317f) * (input.is_sprinting ? 1.25f : 1.0f);
    const float amplitude = std::min(max_amp, raw_amp);
    stride_ += speed * dt * (input.is_sprinting ? 4.5f : 4.0f);

    // Head orientation
    const float head_yaw = input.look_yaw;
    const float head_pitch = input.look_pitch;

    // Body rotation when swinging
    const float body_yaw = (input.swing_progress > 0.0f)
        ? std::sin(std::sqrt(input.swing_progress) * kPi * 2.0f) * 0.2f
        : 0.0f;

    // Issue #64/#65 state precedence: sleeping > riptide > gliding > normal.
    const bool riptiding = !input.is_sleeping && input.riptide > 0.0f;
    const bool gliding = !input.is_sleeping && !riptiding && input.is_gliding;
    glide_age_ = gliding ? glide_age_ + dt : 0.0f;

    root_ = Transform{};
    upper_ = Transform{};

    float glide_phase = 0.f;
    float glide_s = 0.f;
    if (riptiding) {
        // anim_data.js riptide: rig pitch 65 deg, yaw 360 * progress; root pos (0, 16, 0) model px.
        root_.pos = Vec3{0.f, 0.f, 16.0f * kCmPerModelPixel};
        root_.rot = fromEulerXyz(65.0f * kDeg, 360.0f * kDeg * input.riptide, 0.f);
    } else if (gliding) {
        // anim_data.js elytra_fly: rig pitch 80 + 3 sin(phase), roll 2 cos(phase) + bank;
        // root pos (0, 14 + sin(phase), 0) model px.
        glide_phase = 2.0f * kPi * glide_age_ / 1.6f;
        glide_s = std::sin(glide_phase);
        root_.pos = Vec3{0.f, 0.f, (14.0f + glide_s) * kCmPerModelPixel};
        root_.rot = fromEulerXyz((80.0f + 3.0f * glide_s) * kDeg, 0.f,
                                 2.0f * kDeg * std::cos(glide_phase) + input.roll_angle);
    } else if (!input.is_sleeping && input.trident_charge > 0.0f) {
        // anim_data.js trident_charge: upper pitch lerp(0, -4, t), yaw lerp(0, -14, t).
        const float t = clamp01(input.trident_charge / 0.9f);
        upper_.rot = fromEulerXyz(-4.0f * kDeg * t, -14.0f * kDeg * t, 0.f);
    }

    for (size_t i = 0; i < kPartCount; ++i) {
        const auto part = static_cast<StevePart>(i);
        float pitch = 0.f;
        float yaw = 0.f;
        float roll = 0.f;
        Vec3 pos{0.f, 0.f, 0.f};

        if (input.is_sleeping) {
            // Authentic Minecraft sleeping pose:
            // Limbs straight, parallel, aligned with sleeping torso.
            // Subtle breathing oscillation on arms and chest.
            const float breath = std::sin(age_ * 0.1047f) * 0.025f; // ~3.0s cycle
            switch (part) {
                case StevePart::Head:
                case StevePart::Hat:
                    pitch = -0.05f; // Slight pillow tilt
                    yaw = 0.0f;     // Locked head yaw
                    roll = 0.0f;
                    break;
                case StevePart::Body:
                case StevePart::Jacket:
                    pitch = breath * 0.5f;
                    yaw = 0.0f;
                    roll = 0.0f;
                    break;
                case StevePart::RightLeg:
                case StevePart::RightPants:
                case StevePart::LeftLeg:
                case StevePart::LeftPants:
                    pitch = 0.0f;
                    yaw = 0.0f;
                    roll = 0.0f;
                    break;
                case StevePart::RightArm:
                case StevePart::RightSleeve:
                    pitch = 0.0f;
                    yaw = 0.0f;
                    roll = breath;
                    break;
                case StevePart::LeftArm:
                case StevePart::LeftSleeve:
                    pitch = 0.0f;
                    yaw = 0.0f;
                    roll = -breath;
                    break;
                default:
                    break;
            }
        } else if (riptiding) {
            // anim_data.js riptide: the rig spins/pitches via rootTransform(); only local limbs move here.
            switch (part) {
                case StevePart::Head:
                case StevePart::Hat:
                    pitch = -30.0f * kDeg;
                    break;
                case StevePart::Body:
                case StevePart::Jacket:
                    break; // the root carries the torso
                case StevePart::RightArm:
                case StevePart::RightSleeve:
                    pitch = -178.0f * kDeg;
                    roll = -6.0f * kDeg;
                    break;
                case StevePart::LeftArm:
                case StevePart::LeftSleeve:
                    pitch = -178.0f * kDeg;
                    roll = 6.0f * kDeg;
                    break;
                case StevePart::RightLeg:
                case StevePart::RightPants:
                    roll = 1.0f * kDeg;
                    break;
                case StevePart::LeftLeg:
                case StevePart::LeftPants:
                    roll = -1.0f * kDeg;
                    break;
                default:
                    break;
            }
        } else if (gliding) {
            // anim_data.js elytra_fly: the rig pitch/roll and the root bob live in rootTransform().
            switch (part) {
                case StevePart::Head:
                case StevePart::Hat:
                    pitch = -72.0f * kDeg;
                    break;
                case StevePart::Body:
                case StevePart::Jacket:
                    break; // the root carries the torso
                case StevePart::RightArm:
                case StevePart::RightSleeve:
                    pitch = 14.0f * kDeg;
                    roll = (-10.0f + 3.0f * glide_s) * kDeg;
                    break;
                case StevePart::LeftArm:
                case StevePart::LeftSleeve:
                    pitch = 14.0f * kDeg;
                    roll = (10.0f - 3.0f * glide_s) * kDeg;
                    break;
                case StevePart::RightLeg:
                case StevePart::RightPants:
                    pitch = 6.0f * kDeg;
                    roll = -2.0f * kDeg;
                    break;
                case StevePart::LeftLeg:
                case StevePart::LeftPants:
                    pitch = 6.0f * kDeg;
                    roll = 2.0f * kDeg;
                    break;
                default:
                    break;
            }
        } else {
            switch (part) {
                case StevePart::Head:
                case StevePart::Hat:
                    pitch = head_pitch;
                    yaw = head_yaw;
                    if (input.trident_charge > 0.0f) {
                        // anim_data.js trident_charge head: pitch 0, yaw lerp(0, 14, t) relative to upper.
                        const float t = clamp01(input.trident_charge / 0.9f);
                        pitch = 0.0f;
                        yaw = 14.0f * kDeg * t;
                    } else if (input.is_crouching) {
                        // Minecraft ModelBiped: bipedHead.rotationPointY = 1.0F (+1 model pixel down = -5.625 cm canonical Z)
                        pos.z = -5.625f;
                    } else if (input.is_sprinting) {
                        // Compensate head pitch against torso forward pitch to maintain level gaze
                        pitch -= 0.15f;
                    }
                    break;

                case StevePart::Body:
                case StevePart::Jacket:
                    if (input.trident_charge > 0.0f) {
                        // The torso rotation is carried by upperTransform(); keep the part neutral.
                        break;
                    }
                    yaw = body_yaw;
                    if (input.is_crouching) {
                        pitch = 0.5f;
                    } else if (input.is_sprinting) {
                        pitch = 0.15f; // Sprint forward lean (~8.6 degrees)
                    }
                    break;

                case StevePart::RightLeg:
                case StevePart::RightPants:
                    // Authentic Minecraft Java: limb swing is strictly driven by horizontal displacement (speed > 0).
                    // In vertical jump/fall (speed == 0), pitch is 0 and legs remain straight and parallel.
                    pitch = std::cos(stride_ * 0.6662f) * 1.4f * amplitude;
                    if (input.is_crouching) {
                        // Minecraft ModelBiped: rotationPointZ = 4.0F (+4 model pixels back = -22.5 cm canonical X)
                        //                      rotationPointY = 9.0F (-3 model pixels up = +16.875 cm canonical Z)
                        pos.x = -22.5f;
                        pos.z = 16.875f;
                    }
                    break;

                case StevePart::LeftLeg:
                case StevePart::LeftPants:
                    pitch = std::cos(stride_ * 0.6662f + kPi) * 1.4f * amplitude;
                    if (input.is_crouching) {
                        pos.x = -22.5f;
                        pos.z = 16.875f;
                    }
                    break;

                case StevePart::RightArm:
                case StevePart::RightSleeve: {
                    pitch = std::cos(stride_ * 0.6662f + kPi) * amplitude;
                    if (input.arms_sway_only) {
                        pitch = std::sin(age_ * 0.067f) * 0.05f;
                    } else if (input.arms_forward) {
                        pitch = -kPi * 0.5f + std::sin(age_ * 0.067f) * 0.05f; // Minecraft's zombie: arms out in front, a slow sway
                    } else if (input.is_blocking && !input.block_offhand) {
                        pitch = pitch * 0.5f - kPi * 0.3f + head_pitch;
                        yaw = -kPi / 6.0f + head_yaw;
                    } else if (input.is_blocking && input.block_offhand) {
                        // Minecraft ArmPose.ITEM: weapon arm held ready with dampened swing while offhand blocks
                        pitch = pitch * 0.5f - kPi * 0.1f;
                    } else if (input.bow_charge > 0.0f) {
                        pitch = -kPi * 0.5f + head_pitch;
                        yaw = -0.1f + head_yaw - (input.bow_offhand ? 0.4f : 0.0f);
                    } else if (input.trident_charge > 0.0f) {
                        // anim_data.js trident_charge rightArm: pitch lerp(-150, -192, t) + shake, roll -4.
                        const float t = clamp01(input.trident_charge / 0.9f);
                        const float n = input.trident_charge > 0.9f
                            ? std::sin(input.trident_charge * 55.0f) * 1.2f
                            : 0.0f;
                        pitch = (lerp(-150.0f, -192.0f, t) + n) * kDeg;
                        yaw = 0.0f;
                        roll = -4.0f * kDeg;
                    } else if (input.eating_progress > 0.0f) {
                        pitch = -1.7f + std::abs(std::sin(age_ * 1.5f)) * 0.15f;
                        yaw = -0.3f;
                    } else if (input.swing_progress > 0.0f) {
                        float f = 1.0f - input.swing_progress;
                        f *= f;
                        f *= f;
                        const float f1 = std::sin((1.0f - f) * kPi);
                        const float f2 = std::sin(input.swing_progress * kPi) * -(head_pitch - 0.7f) * 0.75f;
                        pitch -= f1 * 1.2f + f2;
                        yaw += body_yaw * 2.0f;
                        roll -= std::sin(input.swing_progress * kPi) * 0.4f;
                    } else {
                        // Idle breathing oscillation
                        roll += std::sin(age_ * 0.067f) * 0.05f;
                    }
                    if (input.is_crouching && input.trident_charge <= 0.0f) {
                        pitch += 0.4f;
                    }
                    break;
                }

                case StevePart::LeftArm:
                case StevePart::LeftSleeve: {
                    pitch = std::cos(stride_ * 0.6662f) * amplitude;
                    if (input.arms_sway_only) {
                        pitch = -std::sin(age_ * 0.067f) * 0.05f;
                    } else if (input.arms_forward) {
                        pitch = -kPi * 0.5f - std::sin(age_ * 0.067f) * 0.05f;
                    } else if (input.is_blocking && input.block_offhand) {
                        pitch = pitch * 0.5f - kPi * 0.3f + head_pitch;
                        yaw = kPi / 6.0f + head_yaw;
                    } else if (input.is_blocking && !input.block_offhand) {
                        pitch = pitch * 0.5f;
                    } else if (input.bow_charge > 0.0f) {
                        pitch = -kPi * 0.5f + head_pitch;
                        yaw = 0.1f + head_yaw + (input.bow_offhand ? 0.0f : 0.4f);
                    } else if (input.trident_charge > 0.0f) {
                        // anim_data.js trident_charge leftArm: pitch lerp(0, -25, t), roll +8.
                        const float t = clamp01(input.trident_charge / 0.9f);
                        pitch = lerp(0.0f, -25.0f, t) * kDeg;
                        yaw = 0.0f;
                        roll = 8.0f * kDeg;
                    } else if (input.swing_progress > 0.0f) {
                        yaw += body_yaw;
                        pitch += body_yaw;
                    } else {
                        roll -= std::sin(age_ * 0.067f) * 0.05f;
                    }
                    if (input.is_crouching && input.trident_charge <= 0.0f) {
                        pitch += 0.4f;
                    }
                    break;
                }

                default:
                    break;
            }
        }

        transforms_[i].pos = pos;
        transforms_[i].rot = fromEuler(pitch, yaw, roll);
    }
}

} // namespace mc
