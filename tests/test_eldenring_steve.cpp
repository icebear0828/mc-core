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
