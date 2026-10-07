#include <gtest/gtest.h>

#include "sekiro_steve.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace sekiro::render;
using mc::StevePart;
using sekiro::native::FQuat;
using sekiro::native::FVector3;

namespace {

constexpr float kPi = 3.14159265358979f;

constexpr StevePart kAllParts[] = {StevePart::Head,       StevePart::Body,       StevePart::RightArm,
                                   StevePart::LeftArm,    StevePart::RightLeg,   StevePart::LeftLeg,
                                   StevePart::Hat,        StevePart::Jacket,     StevePart::RightSleeve,
                                   StevePart::LeftSleeve, StevePart::RightPants, StevePart::LeftPants};

const char* objName(StevePart p) {
    switch (p) {
        case StevePart::Head: return "steve_head";
        case StevePart::Body: return "steve_body";
        case StevePart::RightArm: return "steve_right_arm";
        case StevePart::LeftArm: return "steve_left_arm";
        case StevePart::RightLeg: return "steve_right_leg";
        case StevePart::LeftLeg: return "steve_left_leg";
        case StevePart::Hat: return "steve_hat";
        case StevePart::Jacket: return "steve_jacket";
        case StevePart::RightSleeve: return "steve_right_sleeve";
        case StevePart::LeftSleeve: return "steve_left_sleeve";
        case StevePart::RightPants: return "steve_right_pants";
        case StevePart::LeftPants: return "steve_left_pants";
        default: return "";
    }
}

FQuat yQuat(float radians) {
    return {0.f, std::sin(radians * 0.5f), 0.f, std::cos(radians * 0.5f)};
}

void expectNear(const FVector3& a, const FVector3& b, float eps = 1e-4f) {
    EXPECT_NEAR(a.X, b.X, eps);
    EXPECT_NEAR(a.Y, b.Y, eps);
    EXPECT_NEAR(a.Z, b.Z, eps);
}

} // namespace

// ----------------------------------------------------------------------------- matrix math

TEST(SekiroRenderMathTest, ProductAppliesLeftOperandFirst) {
    const Mat4 m = sekiro::render::translation({1.f, 0.f, 0.f}) * rotationY(kPi * 0.5f); // move +X, then yaw 90deg
    // (0,0,0) -> (1,0,0) -> rotated about Y: +X turns toward -Z
    expectNear(sekiro::render::transformPoint(m, {0.f, 0.f, 0.f}), {0.f, 0.f, -1.f});
}

TEST(SekiroRenderMathTest, RotationYTurnsForwardTowardPlusX) {
    expectNear(sekiro::render::transformPoint(rotationY(kPi * 0.5f), {0.f, 0.f, 1.f}), {1.f, 0.f, 0.f});
    expectNear(sekiro::render::transformPoint(rotationY(kPi), {0.f, 0.f, 1.f}), {0.f, 0.f, -1.f});
    expectNear(sekiro::render::transformPoint(rotationY(0.7f), {0.f, 2.f, 0.f}), {0.f, 2.f, 0.f}); // axis is fixed
}

TEST(SekiroRenderMathTest, QuaternionMatrixMatchesHamiltonRotation) {
    // 90deg about +Y takes +Z to +X (same convention as sekiro_adapter / the unit tests of toNativeQuat)
    expectNear(sekiro::render::transformPoint(rotationFromQuat(yQuat(kPi * 0.5f)), {0.f, 0.f, 1.f}), {1.f, 0.f, 0.f});
    // 90deg about +X takes +Y to +Z
    const FQuat qx{std::sin(kPi * 0.25f), 0.f, 0.f, std::cos(kPi * 0.25f)};
    expectNear(sekiro::render::transformPoint(rotationFromQuat(qx), {0.f, 1.f, 0.f}), {0.f, 0.f, 1.f});
    // identity
    expectNear(sekiro::render::transformPoint(rotationFromQuat(FQuat{}), {3.f, -2.f, 5.f}), {3.f, -2.f, 5.f});
}

TEST(SekiroRenderMathTest, QuaternionMatrixAgreesWithRotationYForAllAngles) {
    for (float a : {0.1f, 0.9f, 2.0f, -1.3f}) {
        expectNear(sekiro::render::transformPoint(rotationFromQuat(yQuat(a)), {0.3f, 0.f, 1.f}), sekiro::render::transformPoint(rotationY(a), {0.3f, 0.f, 1.f}));
    }
}

// ----------------------------------------------------------------------------- camera

namespace {
sekiro::live::LiveSample camera(FVector3 pos, FVector3 right, FVector3 up, FVector3 fwd) {
    sekiro::live::LiveSample s;
    s.cam_pos = pos;
    s.cam_right = right;
    s.cam_up = up;
    s.cam_forward = fwd;
    return s;
}

// clip-space -> NDC, returns {x, y, depth, w}
std::array<float, 4> project(const Mat4& vp, FVector3 p) {
    const auto c = sekiro::render::transform(vp, p);
    return {c[0] / c[3], c[1] / c[3], c[2] / c[3], c[3]};
}
} // namespace

TEST(SekiroRenderCameraTest, PointStraightAheadLandsInTheCentreWithDepthInRange) {
    const auto cam = camera({10.f, 2.f, -3.f}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    const Mat4 vp = viewProjection(cam, 1.0f, 16.f / 9.f, 0.1f, 100.f);
    const auto ndc = project(vp, {10.f, 2.f, 2.f}); // 5 m ahead
    EXPECT_NEAR(ndc[0], 0.f, 1e-5f);
    EXPECT_NEAR(ndc[1], 0.f, 1e-5f);
    EXPECT_GT(ndc[2], 0.f);
    EXPECT_LT(ndc[2], 1.f);
    EXPECT_NEAR(ndc[3], 5.f, 1e-4f);
}

TEST(SekiroRenderCameraTest, RightAndUpMapToPositiveNdc_LeftHanded) {
    const auto cam = camera({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    const Mat4 vp = viewProjection(cam, 1.0f, 1.0f);
    EXPECT_GT(project(vp, {1.f, 0.f, 5.f})[0], 0.f);
    EXPECT_LT(project(vp, {-1.f, 0.f, 5.f})[0], 0.f);
    EXPECT_GT(project(vp, {0.f, 1.f, 5.f})[1], 0.f);
    EXPECT_LT(project(vp, {0.f, -1.f, 5.f})[1], 0.f);
}

TEST(SekiroRenderCameraTest, FieldOfViewSetsTheEdgeOfTheScreen) {
    const auto cam = camera({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    const float fov = 1.0f, aspect = 2.0f;
    const Mat4 vp = viewProjection(cam, fov, aspect);
    const float half_h = std::tan(fov * 0.5f) * 10.f;
    EXPECT_NEAR(project(vp, {0.f, half_h, 10.f})[1], 1.f, 1e-4f);          // vertical FOV
    EXPECT_NEAR(project(vp, {half_h * aspect, 0.f, 10.f})[0], 1.f, 1e-4f); // aspect widens X
}

TEST(SekiroRenderCameraTest, ArbitraryOrientationUsesTheCameraBasis) {
    // yawed 90deg right: forward = +X, right = -Z
    const auto cam = camera({1.f, 0.f, 1.f}, {0, 0, -1}, {0, 1, 0}, {1, 0, 0});
    const Mat4 vp = viewProjection(cam, 1.0f, 1.0f);
    const auto ahead = project(vp, {6.f, 0.f, 1.f});
    EXPECT_NEAR(ahead[0], 0.f, 1e-5f);
    EXPECT_NEAR(ahead[3], 5.f, 1e-4f);
    EXPECT_GT(project(vp, {6.f, 0.f, -1.f})[0], 0.f); // the camera's right side
    EXPECT_LT(project(vp, {-3.f, 0.f, 1.f})[3], 0.f); // behind the camera => negative w
}

// ----------------------------------------------------------------------------- Steve mesh

TEST(SekiroSteveMeshTest, EveryPartIsAClosedBoxOfSixQuads) {
    for (StevePart p : kAllParts) {
        const SteveMesh m = buildPartMesh(p);
        EXPECT_EQ(m.vertices.size(), 24u) << objName(p);
        EXPECT_EQ(m.indices.size(), 36u) << objName(p);
        for (uint16_t i : m.indices) EXPECT_LT(i, m.vertices.size());
    }
}

TEST(SekiroSteveMeshTest, StandsOnTheGroundIs1p8MetresTallAndFacesPlusZ) {
    float min_y = 1e9f, max_y = -1e9f;
    for (StevePart p : {StevePart::Head, StevePart::Body, StevePart::RightLeg, StevePart::LeftLeg, StevePart::RightArm, StevePart::LeftArm}) {
        for (const SteveVertex& v : buildPartMesh(p).vertices) {
            min_y = std::min(min_y, v.y);
            max_y = std::max(max_y, v.y);
        }
    }
    EXPECT_NEAR(min_y, 0.f, 1e-5f);
    EXPECT_NEAR(max_y, 1.8f, 1e-5f);

    // The face (skin region x 8..16, y 8..16) is the quad on the +Z side of the head.
    const SteveMesh head = buildPartMesh(StevePart::Head);
    float front_z = -1e9f;
    for (const auto& v : head.vertices) front_z = std::max(front_z, v.z);
    for (size_t i = 0; i < 4; ++i) {  // first quad of the mesh is the front face
        const auto& v = head.vertices[i];
        EXPECT_NEAR(v.z, front_z, 1e-6f);
        EXPECT_TRUE(std::fabs(v.u - 8.f / 64.f) < 1e-6f || std::fabs(v.u - 16.f / 64.f) < 1e-6f);
        EXPECT_TRUE(std::fabs(v.v - 8.f / 64.f) < 1e-6f || std::fabs(v.v - 16.f / 64.f) < 1e-6f);
    }
}

// Front face rectangles of the standard 64x64 (post-1.8) Minecraft skin layout, written out
// independently of the C++ and Python tables so overlay layers are checked even when the committed
// placeholder skin leaves them empty.
TEST(SekiroSteveMeshTest, FrontFaceUsesTheStandardSkinLayout) {
    struct Case { StevePart part; float u, v, w, h; };
    const Case cases[] = {
        {StevePart::Head, 8, 8, 8, 8},        {StevePart::Hat, 40, 8, 8, 8},
        {StevePart::Body, 20, 20, 8, 12},     {StevePart::Jacket, 20, 36, 8, 12},
        {StevePart::RightArm, 44, 20, 4, 12}, {StevePart::RightSleeve, 44, 36, 4, 12},
        {StevePart::LeftArm, 36, 52, 4, 12},  {StevePart::LeftSleeve, 52, 52, 4, 12},
        {StevePart::RightLeg, 4, 20, 4, 12},  {StevePart::RightPants, 4, 36, 4, 12},
        {StevePart::LeftLeg, 20, 52, 4, 12},  {StevePart::LeftPants, 4, 52, 4, 12},
    };
    for (const Case& c : cases) {
        const SteveMesh m = buildPartMesh(c.part);
        float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
        for (size_t i = 0; i < 4; ++i) {
            u0 = std::min(u0, m.vertices[i].u); u1 = std::max(u1, m.vertices[i].u);
            v0 = std::min(v0, m.vertices[i].v); v1 = std::max(v1, m.vertices[i].v);
        }
        EXPECT_NEAR(u0, c.u / 64.f, 1e-6f) << objName(c.part);
        EXPECT_NEAR(u1, (c.u + c.w) / 64.f, 1e-6f) << objName(c.part);
        EXPECT_NEAR(v0, c.v / 64.f, 1e-6f) << objName(c.part);
        EXPECT_NEAR(v1, (c.v + c.h) / 64.f, 1e-6f) << objName(c.part);
    }
}

TEST(SekiroSteveMeshTest, RightSideOfTheCharacterIsPlusXAndLeftIsMinusX) {
    auto centre_x = [](StevePart p) {
        const SteveMesh m = buildPartMesh(p);
        float sum = 0.f;
        for (const auto& v : m.vertices) sum += v.x;
        return sum / static_cast<float>(m.vertices.size());
    };
    EXPECT_GT(centre_x(StevePart::RightArm), 0.2f);
    EXPECT_LT(centre_x(StevePart::LeftArm), -0.2f);
    EXPECT_GT(centre_x(StevePart::RightLeg), 0.f);
    EXPECT_LT(centre_x(StevePart::LeftLeg), 0.f);
    EXPECT_NEAR(centre_x(StevePart::Body), 0.f, 1e-5f);
}

TEST(SekiroSteveMeshTest, OverlayLayersWrapTheirBaseParts) {
    auto extent = [](StevePart p, float SteveVertex::*axis) {
        const SteveMesh m = buildPartMesh(p);
        float lo = 1e9f, hi = -1e9f;
        for (const auto& v : m.vertices) { lo = std::min(lo, v.*axis); hi = std::max(hi, v.*axis); }
        return std::pair<float, float>(lo, hi);
    };
    const float px = kMetresPerModelPixel;
    const auto head = extent(StevePart::Head, &SteveVertex::x);
    const auto hat = extent(StevePart::Hat, &SteveVertex::x);
    EXPECT_NEAR(hat.first, head.first - 0.5f * px, 1e-5f);
    EXPECT_NEAR(hat.second, head.second + 0.5f * px, 1e-5f);
    const auto body = extent(StevePart::Body, &SteveVertex::y);
    const auto jacket = extent(StevePart::Jacket, &SteveVertex::y);
    EXPECT_NEAR(jacket.second, body.second + 0.25f * px, 1e-5f);
}

TEST(SekiroSteveMeshTest, TrianglesAreWoundOutwardAndUvsStayInsideTheSkin) {
    for (StevePart p : kAllParts) {
        const SteveMesh m = buildPartMesh(p);
        FVector3 c{};
        for (const auto& v : m.vertices) { c.X += v.x; c.Y += v.y; c.Z += v.z; }
        c = c * (1.0f / static_cast<float>(m.vertices.size()));
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            const auto& a = m.vertices[m.indices[i]];
            const auto& b = m.vertices[m.indices[i + 1]];
            const auto& d = m.vertices[m.indices[i + 2]];
            const FVector3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{d.x - a.x, d.y - a.y, d.z - a.z};
            const FVector3 n{e1.Y * e2.Z - e1.Z * e2.Y, e1.Z * e2.X - e1.X * e2.Z, e1.X * e2.Y - e1.Y * e2.X};
            const FVector3 out{a.x - c.X, a.y - c.Y, a.z - c.Z};
            EXPECT_GT(n.X * out.X + n.Y * out.Y + n.Z * out.Z, 0.f) << objName(p) << " triangle " << i / 3;
        }
        for (const auto& v : m.vertices) {
            EXPECT_GE(v.u, 0.f); EXPECT_LE(v.u, 1.f);
            EXPECT_GE(v.v, 0.f); EXPECT_LE(v.v, 1.f);
        }
    }
}

// The repo's OBJ files are produced by tools/extract_mc_assets.py. Every one of their (position, uv)
// samples must be reproduced by the C++ face table, otherwise the two pipelines disagree on the skin.
TEST(SekiroSteveMeshTest, SkinUvMatchesTheCommittedObjModelsVertexForVertex) {
    for (StevePart p : kAllParts) {
        const std::string path = std::string(MC_ASSET_DIR) + "/models/" + objName(p) + ".obj";
        std::ifstream in(path);
        ASSERT_TRUE(in.good()) << path;

        std::vector<ModelPoint> positions;
        std::vector<std::array<float, 2>> uvs;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag == "v") {
                ModelPoint pt;
                ss >> pt.x >> pt.y >> pt.z;
                positions.push_back(pt);
            } else if (tag == "vt") {
                float u, v;
                ss >> u >> v;
                uvs.push_back({u, 1.0f - v}); // the exporter flips V for OBJ
            }
        }
        ASSERT_EQ(positions.size(), uvs.size()) << objName(p);
        const bool overlay = p == StevePart::Hat || p == StevePart::Jacket || p == StevePart::RightSleeve ||
                             p == StevePart::LeftSleeve || p == StevePart::RightPants || p == StevePart::LeftPants;
        if (overlay && positions.empty()) continue; // the committed placeholder skin leaves overlays transparent
        ASSERT_GT(positions.size(), 8u) << objName(p);

        size_t mismatches = 0;
        for (size_t i = 0; i < positions.size(); ++i) {
            const auto candidates = skinUvForLocalPoint(p, positions[i], 2e-3f);
            bool matched = false;
            for (const auto& c : candidates) {
                if (std::fabs(c[0] - uvs[i][0]) < 3e-4f && std::fabs(c[1] - uvs[i][1]) < 3e-4f) matched = true;
            }
            if (!matched) ++mismatches;
        }
        EXPECT_EQ(mismatches, 0u) << objName(p) << ": " << mismatches << " of " << positions.size() << " vertices disagree";
    }
}

// ----------------------------------------------------------------------------- rig

TEST(SekiroSteveRigTest, RestPoseWithRootAtOriginIsTheBareMesh) {
    for (StevePart p : kAllParts) {
        const SteveMesh m = buildPartMesh(p);
        const Mat4 mat = partMatrix(p, FQuat{}, {0.f, 0.f, 0.f}, 0.f);
        for (const auto& v : m.vertices) {
            expectNear(sekiro::render::transformPoint(mat, {v.x, v.y, v.z}), {v.x, v.y, v.z}, 1e-5f);
        }
    }
}

TEST(SekiroSteveRigTest, RootMovesAndYawsTheWholeFigureAboutTheFeet) {
    const FVector3 root{10.f, -35.f, 14.f};
    const Mat4 mat = partMatrix(StevePart::Head, FQuat{}, root, kPi * 0.5f);
    // a point 1 m in front of the feet (+Z) ends up 1 m toward +X of the root after a 90deg yaw
    expectNear(sekiro::render::transformPoint(partMatrix(StevePart::Body, FQuat{}, root, kPi * 0.5f), {0.f, 0.f, 1.f}), {11.f, -35.f, 14.f});
    // the head still sits above the root
    const FVector3 neck = sekiro::render::transformPoint(mat, partPivot(StevePart::Head));
    expectNear(neck, {10.f, -35.f + partPivot(StevePart::Head).Y, 14.f});
}

TEST(SekiroSteveRigTest, PartsRotateAboutTheirPivotNotTheOrigin) {
    for (StevePart p : {StevePart::Head, StevePart::RightArm, StevePart::LeftLeg, StevePart::Body}) {
        const FVector3 pivot = partPivot(p);
        const Mat4 mat = partMatrix(p, yQuat(0.9f), {0.f, 0.f, 0.f}, 0.f);
        expectNear(sekiro::render::transformPoint(mat, pivot), pivot, 1e-5f);
    }
    // ... and a point away from the pivot does move
    const FVector3 foot = {partPivot(StevePart::RightLeg).X, 0.f, 0.f};
    const FVector3 swung = sekiro::render::transformPoint(partMatrix(StevePart::RightLeg, FQuat{std::sin(0.4f), 0.f, 0.f, std::cos(0.4f)}, {}, 0.f), foot);
    EXPECT_GT(std::fabs(swung.Z), 0.1f); // a leg swinging about X moves its foot fore/aft
}

TEST(SekiroSteveRigTest, PivotsMatchTheMinecraftSkeleton) {
    const float px = kMetresPerModelPixel;
    // shoulders 2 px below the neck, 5 px out; hips 12 px below the neck line, 1.9 px out
    EXPECT_NEAR(partPivot(StevePart::Head).Y, 24.f * px, 1e-5f);
    EXPECT_NEAR(partPivot(StevePart::RightArm).Y, 22.f * px, 1e-5f);
    EXPECT_NEAR(partPivot(StevePart::RightArm).X, 5.f * px, 1e-5f);
    EXPECT_NEAR(partPivot(StevePart::LeftArm).X, -5.f * px, 1e-5f);
    EXPECT_NEAR(partPivot(StevePart::RightLeg).Y, 12.f * px, 1e-5f);
    EXPECT_NEAR(partPivot(StevePart::RightLeg).X, 1.9f * px, 1e-5f);
    EXPECT_NEAR(partPivot(StevePart::Hat).Y, partPivot(StevePart::Head).Y, 1e-6f);
}

// ---------------------------------------------------------------------------------------------
// Scene occlusion against the game's depth buffer: reverse-Z, depth ~ kSceneDepthNear / view z.
// Measured on the real game (the Wolf at 4.2 m reads depth 0.0199 -> depth * z = 0.0836 +- body depth).
// ---------------------------------------------------------------------------------------------
TEST(SekiroSceneOcclusionTest, WallInFrontOfSteveHidesHim) {
    const float steve_z = 6.0f;
    const float wall_z = 3.0f;
    EXPECT_TRUE(sceneOccludes(kSceneDepthNear / wall_z, steve_z));
}

TEST(SekiroSceneOcclusionTest, SceneBehindSteveDoesNotHideHim) {
    EXPECT_FALSE(sceneOccludes(kSceneDepthNear / 10.0f, 4.0f));
}

TEST(SekiroSceneOcclusionTest, GroundAtHisFeetDoesNotCutHimOff) {
    // Same surface, calibration error of 5 %: the bias must absorb it.
    const float z = 4.0f;
    EXPECT_FALSE(sceneOccludes(kSceneDepthNear / (z * 0.95f), z));
    EXPECT_FALSE(sceneOccludes(kSceneDepthNear / (z * 1.05f), z));
}

TEST(SekiroSceneOcclusionTest, ClearedDepthMeansNothingIsInTheWay) {
    EXPECT_FALSE(sceneOccludes(0.0f, 4.0f)); // reverse-Z: 0 is the far plane / sky
}

TEST(SekiroSceneOcclusionTest, GarbageDepthNeverHides) {
    EXPECT_FALSE(sceneOccludes(-1.0f, 4.0f));
    EXPECT_FALSE(sceneOccludes(std::nanf(""), 4.0f));
}
