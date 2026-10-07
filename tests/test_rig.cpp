#include <gtest/gtest.h>

#include "mc/rig.hpp"

#include <algorithm>
#include <cmath>

using namespace mc::rig;
using mc::Quat;
using mc::StevePart;
using mc::Vec3;

namespace {

constexpr float kPi = 3.14159265358979f;

constexpr StevePart kAllParts[] = {StevePart::Head,       StevePart::Body,       StevePart::RightArm,
                                   StevePart::LeftArm,    StevePart::RightLeg,   StevePart::LeftLeg,
                                   StevePart::Hat,        StevePart::Jacket,     StevePart::RightSleeve,
                                   StevePart::LeftSleeve, StevePart::RightPants, StevePart::LeftPants};

struct NamedBasis {
    const char* name;
    HostBasis basis;
};

// Four hosts that differ in handedness, up axis and units. Every generic guarantee must hold for all of them.
const NamedBasis kBases[] = {
    {"canonical (Z up, X forward, Y left, cm, right-handed)", HostBasis{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 1.f}},
    {"Sekiro / Unity-like (Y up, Z forward, X right, metres, left-handed)", HostBasis{{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}, 0.01f}},
    {"OpenGL-like (Y up, -Z forward, X right, metres, right-handed)", HostBasis{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, 0.01f}},
    {"Z up, Y forward, X left, 0.75 units per cm, left-handed", HostBasis{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, 0.75f}},
    {"Source-like (Z up, Y forward, X right, 0.75 units per cm, right-handed)", HostBasis{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, 0.75f}},
};

float along(const Vec3& v, const Vec3& axis) { return v.x * axis.x + v.y * axis.y + v.z * axis.z; }

void expectNear(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

Quat axisQuat(const Vec3& axis, float radians) {
    const Vec3 a = axis.normalized();
    const float s = std::sin(radians * 0.5f);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
}

} // namespace

// ----------------------------------------------------------------------------- matrices

TEST(RigMathTest, ProductAppliesLeftOperandFirst) {
    const Mat4 m = translation({1.f, 0.f, 0.f}) * translation({0.f, 2.f, 0.f});
    expectNear(transformPoint(m, {0.f, 0.f, 0.f}), {1.f, 2.f, 0.f});
    const Mat4 rot_then_move = rotationAboutAxis({0.f, 1.f, 0.f}, kPi * 0.5f) * translation({10.f, 0.f, 0.f});
    expectNear(transformPoint(rot_then_move, {0.f, 0.f, 1.f}), {11.f, 0.f, 0.f});
}

TEST(RigMathTest, AxisRotationFollowsTheRightHandRuleOnTheNumbers) {
    expectNear(transformPoint(rotationAboutAxis({0.f, 1.f, 0.f}, kPi * 0.5f), {0.f, 0.f, 1.f}), {1.f, 0.f, 0.f}); // +Z -> +X
    expectNear(transformPoint(rotationAboutAxis({0.f, 0.f, 1.f}, kPi * 0.5f), {1.f, 0.f, 0.f}), {0.f, 1.f, 0.f}); // +X -> +Y
    expectNear(transformPoint(rotationAboutAxis({1.f, 0.f, 0.f}, kPi * 0.5f), {0.f, 1.f, 0.f}), {0.f, 0.f, 1.f}); // +Y -> +Z
    expectNear(transformPoint(rotationAboutAxis({0.f, 3.f, 0.f}, 0.7f), {0.f, 2.f, 0.f}), {0.f, 2.f, 0.f});      // axis is fixed
}

TEST(RigMathTest, QuaternionAndAxisRotationAgreeForArbitraryAxes) {
    for (Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{1, 2, -3}}) {
        for (float a : {0.1f, 0.9f, 2.0f, -1.3f}) {
            expectNear(transformPoint(rotationFromQuat(axisQuat(axis, a)), {0.3f, -0.4f, 1.f}),
                       transformPoint(rotationAboutAxis(axis, a), {0.3f, -0.4f, 1.f}));
        }
    }
    expectNear(transformPoint(rotationFromQuat(Quat{}), {3.f, -2.f, 5.f}), {3.f, -2.f, 5.f});
}

// ----------------------------------------------------------------------------- camera

namespace {
std::array<float, 4> project(const Mat4& vp, Vec3 p) {
    const auto c = transform(vp, p);
    return {c[0] / c[3], c[1] / c[3], c[2] / c[3], c[3]};
}
} // namespace

TEST(RigCameraTest, PointStraightAheadLandsInTheCentreWithDepthInRange) {
    Camera cam;
    cam.position = {10.f, 2.f, -3.f};
    const Mat4 vp = viewProjection(cam, 1.0f, 16.f / 9.f, 0.1f, 100.f);
    const auto ndc = project(vp, {10.f, 2.f, 2.f});
    EXPECT_NEAR(ndc[0], 0.f, 1e-5f);
    EXPECT_NEAR(ndc[1], 0.f, 1e-5f);
    EXPECT_GT(ndc[2], 0.f);
    EXPECT_LT(ndc[2], 1.f);
    EXPECT_NEAR(ndc[3], 5.f, 1e-4f);
}

TEST(RigCameraTest, RightAndUpMapToPositiveNdcAndFieldOfViewSetsTheScreenEdge) {
    const Camera cam;
    const float fov = 1.0f, aspect = 2.0f;
    const Mat4 vp = viewProjection(cam, fov, aspect);
    EXPECT_GT(project(vp, {1.f, 0.f, 5.f})[0], 0.f);
    EXPECT_LT(project(vp, {-1.f, 0.f, 5.f})[0], 0.f);
    EXPECT_GT(project(vp, {0.f, 1.f, 5.f})[1], 0.f);
    const float half_h = std::tan(fov * 0.5f) * 10.f;
    EXPECT_NEAR(project(vp, {0.f, half_h, 10.f})[1], 1.f, 1e-4f);
    EXPECT_NEAR(project(vp, {half_h * aspect, 0.f, 10.f})[0], 1.f, 1e-4f);
    EXPECT_LT(project(vp, {0.f, 0.f, -3.f})[3], 0.f); // behind the camera => negative w
}

// ----------------------------------------------------------------------------- occlusion

TEST(RigOcclusionTest, ReverseZRuleWithAConfiguredConstant) {
    DepthConvention conv;
    conv.depth_times_distance = 0.0806f;
    EXPECT_TRUE(sceneOccludes(conv, 0.0806f / 3.0f, 6.0f));  // a wall at 3 m hides a rig at 6 m
    EXPECT_FALSE(sceneOccludes(conv, 0.0806f / 10.0f, 4.0f)); // scene behind the rig
    EXPECT_FALSE(sceneOccludes(conv, 0.0806f / 3.8f, 4.0f));  // same surface, 5 % calibration error
    EXPECT_FALSE(sceneOccludes(conv, 0.0806f / 4.2f, 4.0f));
    EXPECT_FALSE(sceneOccludes(conv, 0.0f, 4.0f));            // cleared depth: nothing in the way
    EXPECT_FALSE(sceneOccludes(conv, -1.0f, 4.0f));
    EXPECT_FALSE(sceneOccludes(conv, std::nanf(""), 4.0f));
}

TEST(RigOcclusionTest, UncalibratedOrUnsupportedConventionsNeverOcclude) {
    DepthConvention uncalibrated; // constant 0: nobody measured this game yet
    EXPECT_FALSE(sceneOccludes(uncalibrated, 1.0f, 100.0f));
    DepthConvention standard_z;
    standard_z.reverse_z = false;
    standard_z.depth_times_distance = 0.1f;
    EXPECT_FALSE(sceneOccludes(standard_z, 0.9f, 100.0f));
}

// ----------------------------------------------------------------------------- model in every host basis

TEST(RigBasisTest, ReflectionIsDetectedFromTheDeterminant) {
    EXPECT_FALSE(kBases[0].basis.isReflection());
    EXPECT_TRUE(kBases[1].basis.isReflection());
    EXPECT_FALSE(kBases[2].basis.isReflection());
    EXPECT_TRUE(kBases[3].basis.isReflection());
    EXPECT_FALSE(kBases[4].basis.isReflection());
}

TEST(RigMeshTest, EveryPartIsAClosedBoxOfSixQuadsInAnyBasis) {
    for (const auto& nb : kBases) {
        for (StevePart p : kAllParts) {
            const RigMesh m = buildPartMesh(p, nb.basis);
            EXPECT_EQ(m.vertices.size(), 24u) << nb.name;
            EXPECT_EQ(m.indices.size(), 36u) << nb.name;
            for (uint16_t i : m.indices) EXPECT_LT(i, m.vertices.size());
        }
    }
}

TEST(RigMeshTest, StandsOnTheGroundIs1p8MetresTallAndFacesForwardInAnyBasis) {
    for (const auto& nb : kBases) {
        float lo = 1e9f, hi = -1e9f;
        for (StevePart p : {StevePart::Head, StevePart::Body, StevePart::RightLeg, StevePart::LeftLeg, StevePart::RightArm, StevePart::LeftArm}) {
            for (const RigVertex& v : buildPartMesh(p, nb.basis).vertices) {
                const float h = along({v.x, v.y, v.z}, nb.basis.up);
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
        }
        EXPECT_NEAR(lo, 0.f, 1e-3f) << nb.name;
        EXPECT_NEAR(hi, 180.f * nb.basis.units_per_cm, 1e-2f) << nb.name;

        // The face quad (first quad of the head) is on the forward side, spans the skin rectangle 8..16.
        const RigMesh head = buildPartMesh(StevePart::Head, nb.basis);
        float front = -1e9f;
        for (const auto& v : head.vertices) front = std::max(front, along({v.x, v.y, v.z}, nb.basis.forward));
        for (size_t i = 0; i < 4; ++i) {
            const auto& v = head.vertices[i];
            EXPECT_NEAR(along({v.x, v.y, v.z}, nb.basis.forward), front, 1e-4f) << nb.name;
            EXPECT_TRUE(std::fabs(v.u - 8.f / 64.f) < 1e-6f || std::fabs(v.u - 16.f / 64.f) < 1e-6f) << nb.name;
            EXPECT_TRUE(std::fabs(v.v - 8.f / 64.f) < 1e-6f || std::fabs(v.v - 16.f / 64.f) < 1e-6f) << nb.name;
        }
    }
}

TEST(RigMeshTest, TheRightArmIsOnTheCharactersRightInAnyBasis) {
    for (const auto& nb : kBases) {
        auto side = [&](StevePart p) {
            float sum = 0.f;
            const RigMesh m = buildPartMesh(p, nb.basis);
            for (const auto& v : m.vertices) sum += along({v.x, v.y, v.z}, nb.basis.left);
            return sum / static_cast<float>(m.vertices.size());
        };
        // canonical +Y is the character's LEFT
        EXPECT_LT(side(StevePart::RightArm), -10.f * nb.basis.units_per_cm) << nb.name;
        EXPECT_GT(side(StevePart::LeftArm), 10.f * nb.basis.units_per_cm) << nb.name;
        EXPECT_NEAR(side(StevePart::Body), 0.f, 1e-3f) << nb.name;
    }
}

TEST(RigMeshTest, TrianglesAreWoundOutwardAndUvsStayInsideTheSkinInAnyBasis) {
    for (const auto& nb : kBases) {
        for (StevePart p : kAllParts) {
            const RigMesh m = buildPartMesh(p, nb.basis);
            Vec3 c{};
            for (const auto& v : m.vertices) c += Vec3{v.x, v.y, v.z};
            c = c * (1.0f / static_cast<float>(m.vertices.size()));
            for (size_t i = 0; i < m.indices.size(); i += 3) {
                const auto& a = m.vertices[m.indices[i]];
                const auto& b = m.vertices[m.indices[i + 1]];
                const auto& d = m.vertices[m.indices[i + 2]];
                const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{d.x - a.x, d.y - a.y, d.z - a.z};
                const Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
                EXPECT_GT(along(n, {a.x - c.x, a.y - c.y, a.z - c.z}), 0.f) << nb.name << " triangle " << i / 3;
            }
            for (const auto& v : m.vertices) {
                EXPECT_GE(v.u, 0.f);
                EXPECT_LE(v.u, 1.f);
                EXPECT_GE(v.v, 0.f);
                EXPECT_LE(v.v, 1.f);
            }
        }
    }
}

TEST(RigMeshTest, PivotsMatchTheMinecraftSkeletonInAnyBasis) {
    for (const auto& nb : kBases) {
        const float px = kCmPerModelPixel * nb.basis.units_per_cm;
        auto up = [&](StevePart p) { return along(partPivot(p, nb.basis), nb.basis.up); };
        auto lat = [&](StevePart p) { return along(partPivot(p, nb.basis), nb.basis.left); };
        EXPECT_NEAR(up(StevePart::Head), 24.f * px, 1e-3f) << nb.name;
        EXPECT_NEAR(up(StevePart::RightArm), 22.f * px, 1e-3f) << nb.name;
        EXPECT_NEAR(lat(StevePart::RightArm), -5.f * px, 1e-3f) << nb.name;
        EXPECT_NEAR(lat(StevePart::LeftArm), 5.f * px, 1e-3f) << nb.name;
        EXPECT_NEAR(up(StevePart::RightLeg), 12.f * px, 1e-3f) << nb.name;
        EXPECT_NEAR(lat(StevePart::RightLeg), -1.9f * px, 1e-3f) << nb.name;
        EXPECT_NEAR(up(StevePart::Hat), up(StevePart::Head), 1e-6f) << nb.name;
    }
}

TEST(RigMeshTest, OverlayLayersWrapTheirBasePartsInAnyBasis) {
    for (const auto& nb : kBases) {
        const float px = kCmPerModelPixel * nb.basis.units_per_cm;
        auto extent = [&](StevePart p, const Vec3& axis) {
            float lo = 1e9f, hi = -1e9f;
            for (const auto& v : buildPartMesh(p, nb.basis).vertices) {
                const float a = along({v.x, v.y, v.z}, axis);
                lo = std::min(lo, a);
                hi = std::max(hi, a);
            }
            return std::pair<float, float>(lo, hi);
        };
        const auto head = extent(StevePart::Head, nb.basis.left);
        const auto hat = extent(StevePart::Hat, nb.basis.left);
        EXPECT_NEAR(hat.first, head.first - 0.5f * px, 1e-3f) << nb.name;
        EXPECT_NEAR(hat.second, head.second + 0.5f * px, 1e-3f) << nb.name;
        const auto body = extent(StevePart::Body, nb.basis.up);
        const auto jacket = extent(StevePart::Jacket, nb.basis.up);
        EXPECT_NEAR(jacket.second, body.second + 0.25f * px, 1e-3f) << nb.name;
    }
}

// ----------------------------------------------------------------------------- rig placement

TEST(RigPlacementTest, RestPoseWithRootAtOriginIsTheBareMeshInAnyBasis) {
    for (const auto& nb : kBases) {
        for (StevePart p : kAllParts) {
            const RigMesh m = buildPartMesh(p, nb.basis);
            const Mat4 mat = partMatrix(p, Quat{}, {}, 0.f, nb.basis);
            for (const auto& v : m.vertices) expectNear(transformPoint(mat, {v.x, v.y, v.z}), {v.x, v.y, v.z}, 1e-3f);
        }
    }
}

TEST(RigPlacementTest, CanonicalYawTurnsForwardTowardLeftInAnyBasis) {
    // Canonical yaw is counter-clockwise: +90 degrees takes forward to the character's left. A reflecting
    // host must still see that, which is what yawMatrix is for.
    for (const auto& nb : kBases) {
        const Mat4 yaw = yawMatrix(nb.basis, kPi * 0.5f);
        expectNear(transformPoint(yaw, nb.basis.forward), nb.basis.left, 1e-4f);
        expectNear(transformPoint(yaw, nb.basis.up), nb.basis.up, 1e-4f);
        expectNear(transformPoint(yawMatrix(nb.basis, kPi), nb.basis.forward), nb.basis.forward * -1.f, 1e-4f);
    }
}

TEST(RigPlacementTest, RootMovesAndYawsTheWholeFigureAboutTheFeet) {
    for (const auto& nb : kBases) {
        const Vec3 root = nb.basis.forward * 7.f + nb.basis.left * -3.f;
        const float host_yaw = nb.basis.isReflection() ? -kPi * 0.5f : kPi * 0.5f; // canonical +90 degrees
        const Mat4 mat = partMatrix(StevePart::Body, Quat{}, root, host_yaw, nb.basis);
        // a point 1 unit ahead of the feet ends up 1 unit to the character's left of the root
        expectNear(transformPoint(mat, nb.basis.forward), root + nb.basis.left, 1e-4f);
        // the head still sits above the root
        const Vec3 neck = transformPoint(partMatrix(StevePart::Head, Quat{}, root, host_yaw, nb.basis), partPivot(StevePart::Head, nb.basis));
        expectNear(neck, root + partPivot(StevePart::Head, nb.basis), 1e-3f);
    }
}

TEST(RigPlacementTest, PartsRotateAboutTheirPivotNotTheOrigin) {
    for (const auto& nb : kBases) {
        for (StevePart p : {StevePart::Head, StevePart::RightArm, StevePart::LeftLeg, StevePart::Body}) {
            const Vec3 pivot = partPivot(p, nb.basis);
            const Mat4 mat = partMatrix(p, axisQuat(nb.basis.up, 0.9f), {}, 0.f, nb.basis);
            expectNear(transformPoint(mat, pivot), pivot, 1e-3f);
        }
        // a leg swinging about the lateral axis moves its foot fore/aft
        const Vec3 foot = partPivot(StevePart::RightLeg, nb.basis) - nb.basis.up * partPivot(StevePart::RightLeg, nb.basis).y * 0.f -
                          nb.basis.up * along(partPivot(StevePart::RightLeg, nb.basis), nb.basis.up);
        const Vec3 swung = transformPoint(partMatrix(StevePart::RightLeg, axisQuat(nb.basis.left, 0.8f), {}, 0.f, nb.basis), foot);
        EXPECT_GT(std::fabs(along(swung - foot, nb.basis.forward)), 1e-3f) << nb.name;
    }
}

// ----------------------------------------------------------------------------- skin uv (model space, basis independent)

TEST(RigSkinTest, FrontFaceUsesTheStandardSkinLayout) {
    struct Case { StevePart part; float u, v, w, h; };
    const Case cases[] = {
        {StevePart::Head, 8, 8, 8, 8},        {StevePart::Hat, 40, 8, 8, 8},
        {StevePart::Body, 20, 20, 8, 12},     {StevePart::Jacket, 20, 36, 8, 12},
        {StevePart::RightArm, 44, 20, 4, 12}, {StevePart::RightSleeve, 44, 36, 4, 12},
        {StevePart::LeftArm, 36, 52, 4, 12},  {StevePart::LeftSleeve, 52, 52, 4, 12},
        {StevePart::RightLeg, 4, 20, 4, 12},  {StevePart::RightPants, 4, 36, 4, 12},
        {StevePart::LeftLeg, 20, 52, 4, 12},  {StevePart::LeftPants, 4, 52, 4, 12},
    };
    for (const auto& nb : kBases) {
        for (const Case& c : cases) {
            const RigMesh m = buildPartMesh(c.part, nb.basis);
            float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
            for (size_t i = 0; i < 4; ++i) {
                u0 = std::min(u0, m.vertices[i].u); u1 = std::max(u1, m.vertices[i].u);
                v0 = std::min(v0, m.vertices[i].v); v1 = std::max(v1, m.vertices[i].v);
            }
            EXPECT_NEAR(u0, c.u / 64.f, 1e-6f);
            EXPECT_NEAR(u1, (c.u + c.w) / 64.f, 1e-6f);
            EXPECT_NEAR(v0, c.v / 64.f, 1e-6f);
            EXPECT_NEAR(v1, (c.v + c.h) / 64.f, 1e-6f);
        }
    }
}
