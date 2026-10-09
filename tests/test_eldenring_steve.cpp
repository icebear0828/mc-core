#include <gtest/gtest.h>

#include "eldenring_steve.hpp"

#include <cmath>

using namespace eldenring::render;

TEST(EldenRingSteve, BasisMapsCanonicalForwardToPlusZAndLeftToMinusX) {
    const mc::Vec3 f = kBasis.fromCanonical({100.f, 0.f, 0.f});
    EXPECT_NEAR(f.x, 0.f, 1e-5);
    EXPECT_NEAR(f.y, 0.f, 1e-5);
    EXPECT_NEAR(f.z, 1.f, 1e-5);
    const mc::Vec3 l = kBasis.fromCanonical({0.f, 100.f, 0.f});
    EXPECT_NEAR(l.x, -1.f, 1e-5);
    const mc::Vec3 u = kBasis.fromCanonical({0.f, 0.f, 100.f});
    EXPECT_NEAR(u.y, 1.f, 1e-5);
    EXPECT_TRUE(kBasis.isReflection()); // right-handed canonical space to a left-handed host
}

TEST(EldenRingSteve, YawFromQuaternionMatchesAPureRotationAboutY) {
    const float kPi = 3.14159265f;
    for (float a : {0.f, 0.5f, 1.5707963f, 2.525f, -1.0f, -3.0f}) {
        EXPECT_NEAR(yawFromQuat(0.f, std::sin(a * 0.5f), 0.f, std::cos(a * 0.5f)), std::atan2(std::sin(a), std::cos(a)), 1e-5)
            << "a=" << a;
    }
    // The player's quaternion measured live: (0, 0.953, 0, 0.303) -> about 144.7 degrees.
    EXPECT_NEAR(yawFromQuat(0.f, 0.9530834f, 0.f, 0.3027079f) * 180.f / kPi, 144.7f, 0.2f);
}

TEST(EldenRingSteve, YawIgnoresPitchAndRollOfTheQuaternion) {
    // A quaternion tilted about X by 20 degrees on top of a 30 degree yaw still heads 30 degrees.
    const float yaw = 0.5235988f, tilt = 0.3490659f;
    const float qy = std::sin(yaw * 0.5f), wy = std::cos(yaw * 0.5f);
    const float qx = std::sin(tilt * 0.5f), wx = std::cos(tilt * 0.5f);
    // q = yaw * tilt (tilt applied first): Hamilton product.
    const float x = wy * qx;
    const float y = qy * wx;
    const float z = -qy * qx;
    const float w = wy * wx;
    EXPECT_NEAR(yawFromQuat(x, y, z, w), yaw, 1e-4);
}

TEST(EldenRingSteve, RestPoseHasTwelveFiniteMatricesAndTheFeetStandOnTheGivenPoint) {
    const PartMatrices m = restPoseMatrices({10.f, 2.f, -5.f}, 0.f);
    ASSERT_EQ(m.size(), 12u);
    for (const auto& part : m)
        for (float v : part.m) EXPECT_TRUE(std::isfinite(v));
    // The head sits above the feet point and the legs' lowest vertex touches it (mesh has the feet at the origin).
    const mc::rig::RigMesh legs = mc::rig::buildPartMesh(mc::StevePart::RightLeg, kBasis);
    float lowest = 1e9f;
    for (const auto& v : legs.vertices) {
        const mc::Vec3 p = mc::rig::transformPoint(m[static_cast<size_t>(mc::StevePart::RightLeg)], {v.x, v.y, v.z});
        lowest = std::min(lowest, p.y);
    }
    EXPECT_NEAR(lowest, 2.f, 1e-3);
}

TEST(EldenRingSteve, TurningTheFigureMovesAForwardPointTowardTheHeading) {
    const mc::rig::RigMesh body = mc::rig::buildPartMesh(mc::StevePart::Head, kBasis);
    ASSERT_FALSE(body.vertices.empty());
    // A point 1 m in front of the feet at yaw 90 degrees ends up on the +X side (right) of the feet.
    const PartMatrices m = restPoseMatrices({0.f, 0.f, 0.f}, 1.5707963f);
    const mc::Vec3 front = mc::rig::transformPoint(m[0], {0.f, 0.f, 1.f});
    const mc::Vec3 origin = mc::rig::transformPoint(m[0], {0.f, 0.f, 0.f});
    EXPECT_GT(front.x - origin.x, 0.9f);
    EXPECT_NEAR(front.z - origin.z, 0.f, 0.1f);
}

TEST(EldenRingSteve, HostQuatKeepsTheIdentityAndMapsACanonicalYawToARotationAboutTheHostUpAxis) {
    const mc::Quat id = hostQuat({0.f, 0.f, 0.f, 1.f});
    EXPECT_FLOAT_EQ(id.w, 1.f);
    EXPECT_FLOAT_EQ(id.x, 0.f);
    // A canonical turn about Z (up) becomes a turn about the host's Y (up), with the sign flipped by the reflection.
    const float s = std::sin(0.4f), c = std::cos(0.4f);
    const mc::Quat q = hostQuat({0.f, 0.f, s, c});
    EXPECT_NEAR(q.y, -s, 1e-6);
    EXPECT_NEAR(q.x, 0.f, 1e-6);
    EXPECT_NEAR(q.z, 0.f, 1e-6);
}

TEST(EldenRingSteve, MotionReportsForwardSpeedFromTheHeading) {
    SteveMotion m;
    float feet[3] = {0.f, 0.f, 0.f};
    m.update(0.016f, feet, 0.f); // first sample: no speed yet
    mc::SteveAnimInput in;
    for (int i = 0; i < 40; ++i) {
        feet[2] += 4.f * 0.016f; // 4 m/s along +Z while facing +Z
        in = m.update(0.016f, feet, 0.f);
    }
    EXPECT_NEAR(in.forward_speed, 4.f, 0.1f);
    EXPECT_NEAR(in.strafe_speed, 0.f, 0.1f);
    // Walking the same way while facing +X is a sideways movement for the character.
    SteveMotion side;
    float f2[3] = {0.f, 0.f, 0.f};
    side.update(0.016f, f2, 1.5707963f);
    for (int i = 0; i < 40; ++i) {
        f2[2] += 3.f * 0.016f;
        in = side.update(0.016f, f2, 1.5707963f);
    }
    EXPECT_NEAR(in.forward_speed, 0.f, 0.1f);
    EXPECT_GT(std::abs(in.strafe_speed), 2.5f);
}

TEST(EldenRingSteve, MotionIgnoresATeleportOrOriginRebase) {
    SteveMotion m;
    float feet[3] = {0.f, 0.f, 0.f};
    m.update(0.016f, feet, 0.f);
    for (int i = 0; i < 10; ++i) {
        feet[2] += 0.06f;
        m.update(0.016f, feet, 0.f);
    }
    feet[0] += 32.f; // the floating origin moved
    const mc::SteveAnimInput in = m.update(0.016f, feet, 0.f);
    EXPECT_NEAR(in.forward_speed, 0.f, 1e-6);
    EXPECT_NEAR(in.strafe_speed, 0.f, 1e-6);
}

TEST(EldenRingSteve, PosedMatricesFollowTheAnimatorAndStayFinite) {
    mc::SteveAnimator anim;
    mc::SteveAnimInput in;
    in.forward_speed = 4.f;
    for (int i = 0; i < 30; ++i) anim.update(0.016f, in);
    const PartMatrices posed = posedMatrices(anim.getTransforms(), {1.f, 2.f, 3.f}, 0.5f);
    const PartMatrices rest = restPoseMatrices({1.f, 2.f, 3.f}, 0.5f);
    bool any_different = false;
    for (size_t i = 0; i < posed.size(); ++i) {
        for (size_t k = 0; k < 16; ++k) {
            EXPECT_TRUE(std::isfinite(posed[i].m[k]));
            if (std::abs(posed[i].m[k] - rest[i].m[k]) > 1e-4f) any_different = true;
        }
    }
    EXPECT_TRUE(any_different); // legs and arms swing while walking
}

TEST(EldenRingSteve, DeathFlipFollowsMinecraftsCurve) {
    using namespace eldenring::render;
    EXPECT_FLOAT_EQ(deathFlipFraction(0.f), 0.f);
    EXPECT_FLOAT_EQ(deathFlipFraction(-1.f), 0.f);
    EXPECT_NEAR(deathFlipFraction(0.05f), 0.f, 1e-3);                     // 1 tick: (1 - 1) / 20 * 1.6 = 0
    EXPECT_NEAR(deathFlipFraction(0.25f), std::sqrt(4.f / 20.f * 1.6f), 1e-5); // 5 ticks
    EXPECT_FLOAT_EQ(deathFlipFraction(0.7f), 1.f);                         // fully flipped from about 0.68 s
    EXPECT_FLOAT_EQ(deathFlipFraction(5.f), 1.f);
    EXPECT_LT(deathFlipFraction(0.2f), deathFlipFraction(0.4f));           // monotonic
}

TEST(EldenRingSteve, DeathFallLaysTheFigureOnItsSide) {
    using namespace eldenring::render;
    const mc::Vec3 feet{4.f, 1.f, 7.f};
    const float yaw = 0.6f;
    const mc::Vec3 head{4.f, 2.8f, 7.f}; // 1.8 m straight above the feet
    const auto none = mc::rig::transformPoint(deathFallMatrix(feet, yaw, 0.f), head);
    EXPECT_NEAR(none.x, head.x, 1e-4);
    EXPECT_NEAR(none.y, head.y, 1e-4);
    EXPECT_NEAR(none.z, head.z, 1e-4);
    const auto lying = mc::rig::transformPoint(deathFallMatrix(feet, yaw, 1.f), head);
    EXPECT_NEAR(lying.y, 1.f, 1e-3);                                       // at the height of the feet
    const float dx = lying.x - feet.x, dz = lying.z - feet.z;
    EXPECT_NEAR(std::sqrt(dx * dx + dz * dz), 1.8f, 1e-3);                 // 1.8 m to the side
    // the side is perpendicular to the heading
    EXPECT_NEAR(dx * std::sin(yaw) + dz * std::cos(yaw), 0.f, 1e-3);
}

#include "mc/item_model.hpp"

// The third-person held item is built in the right arm's rest pose and drawn with the right arm's matrix: with the ER basis it must
// end up next to the hand of a standing Steve (metres, y up), not at the feet or kilometres away.
TEST(EldenRingSteve, HeldItemSitsAtTheRightHandWithTheErBasis) {
    std::vector<uint8_t> atlas(16 * 16 * 4, 0);
    for (int y = 2; y < 14; ++y) {
        for (int x = 7; x < 9; ++x) atlas[(static_cast<size_t>(y) * 16 + x) * 4 + 3] = 255; // a vertical bar
    }
    const mc::rig::ItemSprite sprite{atlas.data(), 16, 16, 0, 0, 16, 16};
    const mc::rig::RigMesh mesh = mc::rig::buildHeldItemMesh(sprite, mc::rig::HeldItemStyle::Handheld, eldenring::render::kBasis);
    ASSERT_FALSE(mesh.vertices.empty());
    const mc::rig::Mat4 arm = mc::rig::partMatrix(mc::StevePart::RightArm, mc::Quat{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}, 0.f, eldenring::render::kBasis);
    float lo_y = 1e9f, hi_y = -1e9f, max_horizontal = 0.f;
    for (const mc::rig::RigVertex& v : mesh.vertices) {
        const mc::Vec3 p = mc::rig::transformPoint(arm, {v.x, v.y, v.z});
        lo_y = std::min(lo_y, p.y);
        hi_y = std::max(hi_y, p.y);
        max_horizontal = std::max(max_horizontal, std::sqrt(p.x * p.x + p.z * p.z));
    }
    EXPECT_GT(lo_y, 0.2f);
    EXPECT_LT(hi_y, 1.9f);
    EXPECT_LT(max_horizontal, 1.2f);
}
