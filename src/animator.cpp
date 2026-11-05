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

// One arm pose for both hands. The old right/left blocks were copies with different signs; the
// only real asymmetries are the authored trident curves (the holding arm thrusts, the other
// braces) and the swing arc (only the attacking arm follows it). `hand` is the arm being posed.
//
// The action branches (shield, bow, trident, eating, swing) are shared with the gliding pose, which
// keeps its elytra arm base when no action is active. Returns true when an action took over the arm.
bool poseHandAction(const SteveAnimInput& in, Hand hand, float head_yaw, float head_pitch,
                    float body_yaw, float& pitch, float& yaw, float& roll) {
    const bool right = hand == Hand::Main;
    const HandItem& self = in.hands[hand];
    const HandItem& other = in.hands.other(hand);
    const bool self_trident = self.use == HandUse::TridentCharge;
    const bool other_trident = other.use == HandUse::TridentCharge;

    if (self.use == HandUse::Block && self.raised) {
        // Minecraft ArmPose.BLOCK: the shield arm is up and follows the look.
        pitch = pitch * 0.5f - kPi * 0.3f + head_pitch;
        yaw = (right ? -kPi / 6.f : kPi / 6.f) + head_yaw;
    } else if (other.use == HandUse::Block && other.raised) {
        // ArmPose.ITEM: the free arm holds its item ready while the offhand blocks.
        pitch = pitch * 0.5f - kPi * 0.1f;
        yaw = 0.f;
    } else if (self.use == HandUse::BowDraw || other.use == HandUse::BowDraw) {
        // Both arms are raised; the bow hand holds the bow, the other draws the string across the chest.
        pitch = -kPi * 0.5f + head_pitch;
        yaw = (right ? -0.1f : 0.1f) + head_yaw;
        if (self.use != HandUse::BowDraw) yaw += right ? -0.4f : 0.4f;
    } else if (self_trident || other_trident) {
        const HandItem& trident = self_trident ? self : other;
        const float t = clamp01(trident.progress / 0.9f);
        if (self_trident) {
            // anim_data.js trident_charge holding arm: pitch lerp(-150, -192, t) + shake, roll -4.
            const float n = trident.progress > 0.9f ? std::sin(trident.progress * 55.0f) * 1.2f : 0.0f;
            pitch = (lerp(-150.0f, -192.0f, t) + n) * kDeg;
            yaw = 0.f;
            roll = (right ? -4.f : 4.f) * kDeg;
        } else {
            // The other arm braces: pitch lerp(0, -25, t), roll +8 (mirrored for the right arm).
            pitch = lerp(0.0f, -25.0f, t) * kDeg;
            yaw = 0.f;
            roll = (right ? -8.f : 8.f) * kDeg;
        }
    } else if (self.use == HandUse::Eat) {
        // anim_data.js eat: -72 deg with a 6 deg chew, yaw 32 deg toward the mouth (mirrored).
        const float chew = std::abs(std::sin(6.0f * kPi * self.progress));
        pitch = (-72.0f + 6.0f * chew) * kDeg;
        yaw = (right ? 32.0f : -32.0f) * kDeg;
        roll = 0.0f;
    } else if (in.swing_progress > 0.0f) {
        // Java applies the attack swing on top of the hand's ArmPose.ITEM base.
        if (self.present) {
            pitch = pitch * 0.5f - kPi * 0.1f;
            yaw = 0.0f;
        }
        if (hand == in.swing_hand) {
            float f = 1.0f - in.swing_progress;
            f *= f;
            f *= f;
            const float f1 = std::sin((1.0f - f) * kPi);
            const float f2 = std::sin(in.swing_progress * kPi) * -(head_pitch - 0.7f) * 0.75f;
            pitch -= f1 * 1.2f + f2;
            yaw += body_yaw * 2.0f;
            roll -= (hand == Hand::Main ? 1.0f : -1.0f) * std::sin(in.swing_progress * kPi) * 0.4f;
        } else {
            // The other arm follows the torso twist without its own arc.
            yaw += body_yaw;
            pitch += body_yaw;
        }
    } else {
        return false;
    }
    return true;
}

void poseArm(const SteveAnimInput& in, Hand hand, float age, float stride, float amplitude,
             float head_yaw, float head_pitch, float body_yaw, float& pitch, float& yaw, float& roll) {
    const bool right = hand == Hand::Main;
    const HandItem& self = in.hands[hand];
    const HandItem& other = in.hands.other(hand);
    const bool trident_active = self.use == HandUse::TridentCharge || other.use == HandUse::TridentCharge;

    pitch = std::cos(stride * 0.6662f + (right ? kPi : 0.f)) * amplitude;
    if (in.arms_sway_only) {
        pitch = (right ? 1.f : -1.f) * std::sin(age * 0.067f) * 0.05f;
    } else if (in.arms_forward) {
        pitch = -kPi * 0.5f + (right ? 1.f : -1.f) * std::sin(age * 0.067f) * 0.05f;
    } else if (!poseHandAction(in, hand, head_yaw, head_pitch, body_yaw, pitch, yaw, roll)) {
        if (self.present) {
            // Minecraft ArmPose.ITEM: half the walk swing, lowered 18 degrees.
            pitch = pitch * 0.5f - kPi * 0.1f;
            yaw = 0.0f;
        } else {
            // Idle breathing oscillation
            roll += (right ? 1.f : -1.f) * std::sin(age * 0.067f) * 0.05f;
        }
    }
    if (in.is_crouching && !trident_active) {
        pitch += 0.4f;
    }
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

    // The hand plan is the only source of the use actions; the head/torso follow whichever hand is using one.
    const HandItem* trident_use = input.hands.findUse(HandUse::TridentCharge);
    const float trident_charge = trident_use != nullptr ? trident_use->progress : 0.f;
    const HandItem* eat_use = input.hands.findUse(HandUse::Eat);
    const float eating_progress = eat_use != nullptr ? eat_use->progress : 0.f;

    // Body rotation when swinging: the torso twists towards the arm that swings.
    const float swing_side = input.swing_hand == Hand::Main ? 1.0f : -1.0f;
    const float body_yaw = (input.swing_progress > 0.0f)
        ? swing_side * std::sin(std::sqrt(input.swing_progress) * kPi * 2.0f) * 0.2f
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
    } else if (!input.is_sleeping && trident_charge > 0.0f) {
        // anim_data.js trident_charge: upper pitch lerp(0, -4, t), yaw lerp(0, -14, t).
        const float t = clamp01(trident_charge / 0.9f);
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
                    if (eating_progress > 0.0f) {
                        // Issue #179: the head still chews while the eating arm plays its pose.
                        const float chew = std::abs(std::sin(6.0f * kPi * eating_progress));
                        pitch += (8.0f + 4.0f * chew) * kDeg;
                        yaw -= 6.0f * kDeg;
                    }
                    break;
                case StevePart::Body:
                case StevePart::Jacket:
                    break; // the root carries the torso
                case StevePart::RightArm:
                case StevePart::RightSleeve: {
                    float action_pitch = 0.f;
                    float action_yaw = 0.f;
                    float action_roll = 0.f;
                    // Issue #179: a hand action replaces the elytra arm pose; the wings are separate props,
                    // so eating, drawing, charging and swinging still animate on top of the glide.
                    if (poseHandAction(input, Hand::Main, head_yaw, head_pitch, body_yaw,
                                       action_pitch, action_yaw, action_roll)) {
                        pitch = action_pitch;
                        yaw = action_yaw;
                        roll = action_roll;
                    } else {
                        pitch = 14.0f * kDeg;
                        roll = (-10.0f + 3.0f * glide_s) * kDeg;
                    }
                    break;
                }
                case StevePart::LeftArm:
                case StevePart::LeftSleeve: {
                    float action_pitch = 0.f;
                    float action_yaw = 0.f;
                    float action_roll = 0.f;
                    if (poseHandAction(input, Hand::Off, head_yaw, head_pitch, body_yaw,
                                       action_pitch, action_yaw, action_roll)) {
                        pitch = action_pitch;
                        yaw = action_yaw;
                        roll = action_roll;
                    } else {
                        pitch = 14.0f * kDeg;
                        roll = (10.0f - 3.0f * glide_s) * kDeg;
                    }
                    break;
                }
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
                    if (trident_charge > 0.0f) {
                        // anim_data.js trident_charge head: pitch 0, yaw lerp(0, 14, t) relative to upper.
                        const float t = clamp01(trident_charge / 0.9f);
                        pitch = 0.0f;
                        yaw = 14.0f * kDeg * t;
                    } else if (input.is_crouching) {
                        // Minecraft ModelBiped: bipedHead.rotationPointY = 1.0F (+1 model pixel down = -5.625 cm canonical Z)
                        pos.z = -5.625f;
                    } else if (input.is_sprinting) {
                        // Compensate head pitch against torso forward pitch to maintain level gaze
                        pitch -= 0.15f;
                    }
                    if (eating_progress > 0.0f) {
                        // anim_data.js eat: head 8 deg down plus a 4 deg chew, turned 6 deg.
                        const float chew = std::abs(std::sin(6.0f * kPi * eating_progress));
                        pitch += (8.0f + 4.0f * chew) * kDeg;
                        yaw -= 6.0f * kDeg;
                    }
                    break;

                case StevePart::Body:
                case StevePart::Jacket:
                    if (trident_charge > 0.0f) {
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
                case StevePart::RightSleeve:
                    poseArm(input, Hand::Main, age_, stride_, amplitude, head_yaw, head_pitch, body_yaw, pitch, yaw, roll);
                    break;

                case StevePart::LeftArm:
                case StevePart::LeftSleeve:
                    poseArm(input, Hand::Off, age_, stride_, amplitude, head_yaw, head_pitch, body_yaw, pitch, yaw, roll);
                    break;

                default:
                    break;
            }
        }

        transforms_[i].pos = pos;
        transforms_[i].rot = fromEuler(pitch, yaw, roll);
    }
}

} // namespace mc
