#include "mc/animator.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace mc {

namespace {
constexpr float kPi = std::numbers::pi_v<float>;

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
} // namespace

SteveAnimator::SteveAnimator() {
    reset();
}

void SteveAnimator::reset() {
    age_ = 0.f;
    stride_ = 0.f;
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
        } else if (input.is_gliding) {
            switch (part) {
                case StevePart::Head:
                case StevePart::Hat:
                    pitch = 0.8f + head_pitch * 0.5f;
                    yaw = head_yaw;
                    roll = input.roll_angle * 0.3f;
                    break;
                case StevePart::Body:
                case StevePart::Jacket:
                    pitch = -1.4f + input.look_pitch;
                    yaw = body_yaw;
                    roll = input.roll_angle;
                    break;
                case StevePart::RightLeg:
                case StevePart::RightPants:
                    pitch = 0.1f;
                    roll = 0.05f;
                    break;
                case StevePart::LeftLeg:
                case StevePart::LeftPants:
                    pitch = 0.1f;
                    roll = -0.05f;
                    break;
                case StevePart::RightArm:
                case StevePart::RightSleeve:
                    pitch = 0.2f;
                    roll = 0.1f;
                    break;
                case StevePart::LeftArm:
                case StevePart::LeftSleeve:
                    pitch = 0.2f;
                    roll = -0.1f;
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
                    if (input.is_crouching) {
                        // Minecraft ModelBiped: bipedHead.rotationPointY = 1.0F (+1 model pixel down = -5.625 cm canonical Z)
                        pos.z = -5.625f;
                    } else if (input.is_sprinting) {
                        // Compensate head pitch against torso forward pitch to maintain level gaze
                        pitch -= 0.15f;
                    }
                    break;

                case StevePart::Body:
                case StevePart::Jacket:
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
                        pitch = pitch * 0.5f - kPi;
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
                    if (input.is_crouching) {
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
                    } else if (input.swing_progress > 0.0f) {
                        yaw += body_yaw;
                        pitch += body_yaw;
                    } else {
                        roll -= std::sin(age_ * 0.067f) * 0.05f;
                    }
                    if (input.is_crouching) {
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
