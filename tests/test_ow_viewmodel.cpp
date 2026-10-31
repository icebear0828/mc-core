#include <gtest/gtest.h>

#include "ow_viewmodel.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <vector>

namespace {

using eldenring::ow::OwAnimator;
using eldenring::ow::OwModel;
using eldenring::ow::parseOwModel;

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
void appendI32(std::vector<uint8_t>& b, int32_t v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
void appendF32(std::vector<uint8_t>& b, float v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
void appendFloats(std::vector<uint8_t>& b, std::initializer_list<float> values) {
    for (float f : values) appendF32(b, f);
}
void appendName(std::vector<uint8_t>& b, const char* name) {
    const uint32_t len = static_cast<uint32_t>(std::strlen(name));
    appendU32(b, len);
    b.insert(b.end(), name, name + len);
}
void appendMatrix(std::vector<uint8_t>& b, const float* m) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(m);
    b.insert(b.end(), p, p + 16 * sizeof(float));
}

// One triangle, one rigid submesh, CSVM v1.
std::vector<uint8_t> makeV1() {
    std::vector<uint8_t> b;
    b.insert(b.end(), {'C', 'S', 'V', 'M'});
    appendU32(b, 1); // version
    appendU32(b, 1); // submeshes
    appendU32(b, 3); // vertices
    appendU32(b, 3); // indices
    appendU32(b, 1);
    b.push_back('a');
    appendU32(b, 0); // bone index
    appendU32(b, 0); // first index
    appendI32(b, 0); // base vertex
    appendU32(b, 3); // index count
    for (int v = 0; v < 3; ++v) {
        appendFloats(b, {static_cast<float>(v), 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f});
    }
    appendU32(b, 0);
    appendU32(b, 1);
    appendU32(b, 2);
    return b;
}

// CSVM v4: one bone, one skinned triangle, `clips` named tracks.
std::vector<uint8_t> makeV4(const char* bone_name, uint8_t joint, bool with_clips, uint32_t clip_frames = 2) {
    std::vector<uint8_t> b;
    const eldenring::fp::M4 identity;
    b.insert(b.end(), {'C', 'S', 'V', 'M'});
    appendU32(b, 4);
    appendU32(b, 1); // submeshes
    appendU32(b, 3); // vertices
    appendU32(b, 3); // indices
    appendName(b, "gauntlet_0");
    appendU32(b, 0); // bone index
    appendU32(b, 0); // first index
    appendI32(b, 0); // base vertex
    appendU32(b, 3); // index count
    appendU32(b, 1); // flags: skinned

    appendU32(b, 1); // one bone
    appendName(b, bone_name);
    appendMatrix(b, identity.m.data());

    for (int v = 0; v < 3; ++v) {
        appendFloats(b, {static_cast<float>(v), 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f});
        b.push_back(joint);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
        b.push_back(255);
        b.push_back(0);
        b.push_back(0);
        b.push_back(0);
    }
    appendU32(b, 0);
    appendU32(b, 1);
    appendU32(b, 2);

    appendU32(b, with_clips ? 1u : 0u);
    if (with_clips) {
        appendName(b, "idle");
        appendF32(b, 10.0f);
        appendU32(b, clip_frames);
        appendU32(b, 1); // one track
        appendName(b, bone_name);
        for (uint32_t f = 0; f < clip_frames; ++f) {
            eldenring::fp::M4 m;
            m.m[11] = static_cast<float>(f); // z = frame index
            appendMatrix(b, m.m.data());
        }
    }
    return b;
}

OwModel makeAnimModel() {
    OwModel model;
    model.vertices.resize(3);
    model.indices = {0, 1, 2};
    eldenring::ow::OwSubmesh sub;
    sub.index_count = 3;
    sub.flags = 1;
    model.submeshes.push_back(sub);

    eldenring::ow::OwBone bone;
    bone.name = "Hand";
    model.bones.push_back(bone);

    auto makeClip = [](const char* name, float fps, std::vector<eldenring::fp::M4> frames) {
        eldenring::ow::OwAnimClip clip;
        clip.name = name;
        clip.fps = fps;
        clip.frame_count = static_cast<uint32_t>(frames.size());
        clip.tracks.resize(1);
        clip.tracks[0] = std::move(frames);
        return clip;
    };

    auto at = [](float z) {
        eldenring::fp::M4 m;
        m.m[11] = z;
        return m;
    };

    model.clips["idle"] = makeClip("idle", 10.0f, {at(0.0f), at(1.0f), at(2.0f)});
    model.clips["punch"] = makeClip("punch", 10.0f, {at(10.0f), at(11.0f), at(12.0f)});
    model.clips["shoot"] = makeClip("shoot", 10.0f, {at(20.0f), at(21.0f)});
    return model;
}

TEST(OwViewmodelTest, ParsesRigidCsvmV1) {
    const std::vector<uint8_t> bytes = makeV1();
    OwModel model;
    ASSERT_TRUE(parseOwModel(bytes.data(), bytes.size(), model));
    EXPECT_EQ(model.vertices.size(), 3u);
    EXPECT_EQ(model.indices.size(), 3u);
    ASSERT_EQ(model.submeshes.size(), 1u);
    EXPECT_EQ(model.submeshes[0].index_count, 3u);
    EXPECT_FALSE(model.submeshes[0].skinned());
    EXPECT_FLOAT_EQ(model.vertices[2].pos[0], 2.f);
    EXPECT_FLOAT_EQ(model.vertices[0].normal[2], 1.f);
}

TEST(OwViewmodelTest, RejectsBadMagicAndTruncatedInput) {
    std::vector<uint8_t> bytes = makeV1();
    bytes[0] = 'X';
    OwModel model;
    EXPECT_FALSE(parseOwModel(bytes.data(), bytes.size(), model));

    bytes = makeV1();
    EXPECT_FALSE(parseOwModel(bytes.data(), 10, model));
    EXPECT_FALSE(parseOwModel(nullptr, 0, model));
}

TEST(OwViewmodelTest, ParsesSkinnedV4WithClips) {
    const std::vector<uint8_t> bytes = makeV4("Hand", 0, true, 2);
    OwModel model;
    ASSERT_TRUE(parseOwModel(bytes.data(), bytes.size(), model));
    ASSERT_EQ(model.bones.size(), 1u);
    EXPECT_EQ(model.bones[0].name, "Hand");
    ASSERT_TRUE(model.submeshes[0].skinned());
    EXPECT_EQ(model.vertices[0].joints[0], 0);
    EXPECT_EQ(model.vertices[0].weights[0], 255);
    ASSERT_EQ(model.clips.count("idle"), 1u);
    const eldenring::ow::OwAnimClip& clip = model.clips.at("idle");
    EXPECT_FLOAT_EQ(clip.fps, 10.0f);
    ASSERT_EQ(clip.frame_count, 2u);
    ASSERT_EQ(clip.tracks.size(), 1u);
    ASSERT_EQ(clip.tracks[0].size(), 2u);
    EXPECT_FLOAT_EQ(clip.tracks[0][1].m[11], 1.0f);
    EXPECT_FLOAT_EQ(model.clipDuration("idle"), 0.1f);
}

TEST(OwViewmodelTest, RejectsSkinnedV4WithoutClips) {
    const std::vector<uint8_t> bytes = makeV4("Hand", 0, false);
    OwModel model;
    EXPECT_FALSE(parseOwModel(bytes.data(), bytes.size(), model));
}

TEST(OwViewmodelTest, RejectsOutOfRangeJoint) {
    const std::vector<uint8_t> bytes = makeV4("Hand", 3, true);
    OwModel model;
    EXPECT_FALSE(parseOwModel(bytes.data(), bytes.size(), model));
}

TEST(OwViewmodelTest, AnimatorSamplesClipFramesAndInterpolates) {
    OwModel model = makeAnimModel();
    OwAnimator anim;
    anim.setModel(&model);

    anim.tick(0.016f, 3, 0.0f);
    ASSERT_EQ(anim.palette().size(), 1u);
    EXPECT_FLOAT_EQ(anim.palette()[0].m[11], 10.0f);

    anim.tick(0.016f, 3, 0.15f);
    EXPECT_NEAR(anim.palette()[0].m[11], 11.5f, 1e-4f);
}

TEST(OwViewmodelTest, AnimatorLoopsTheIdleClip) {
    OwModel model = makeAnimModel();
    OwAnimator anim;
    anim.setModel(&model);

    anim.tick(0.016f, 0, 0.1f);
    EXPECT_NEAR(anim.palette()[0].m[11], 1.0f, 1e-4f) << "idle frame 1";

    // The clip is 3 frames at 10 fps: 0.35 s wraps to 0.05 s, i.e. frame 0.5.
    anim.tick(0.016f, 0, 0.35f);
    EXPECT_NEAR(anim.palette()[0].m[11], 0.5f, 1e-4f);
}

TEST(OwViewmodelTest, AnimatorCrossfadesBetweenStates) {
    OwModel model = makeAnimModel();
    OwAnimator anim;
    anim.setModel(&model);

    // Establish the idle pose at idle frame 0 (z = 0).
    anim.tick(0.016f, 0, 0.0f);
    ASSERT_NEAR(anim.palette()[0].m[11], 0.0f, 1e-4f);

    // Switching to punch frame 0 (z = 10) blends over 80 ms instead of popping.
    // The first tick starts the fade from the current pose.
    anim.tick(0.016f, 3, 0.0f);
    EXPECT_NEAR(anim.palette()[0].m[11], 0.0f, 1e-4f);

    anim.tick(0.016f, 3, 0.0f);
    EXPECT_GT(anim.palette()[0].m[11], 0.0f);
    EXPECT_LT(anim.palette()[0].m[11], 10.0f);

    for (int i = 0; i < 10; ++i) anim.tick(0.016f, 3, 0.0f);
    EXPECT_NEAR(anim.palette()[0].m[11], 10.0f, 1e-4f);
}

TEST(OwViewmodelTest, AnimatorMapsStatesToAuthoredClips) {
    OwModel model = makeAnimModel();
    OwAnimator anim;
    anim.setModel(&model);

    anim.tick(0.016f, 2, 0.0f);
    EXPECT_NEAR(anim.palette()[0].m[11], 20.0f, 1e-4f) << "state 2 plays the shoot clip";

    anim.tick(0.016f, 4, 0.0f); // no uppercut clip: falls back to idle
    EXPECT_TRUE(std::isfinite(anim.palette()[0].m[11]));
    EXPECT_EQ(std::string(eldenring::ow::owClipNameForState(4)), "uppercut");
}

TEST(OwViewmodelTest, ClipDurationsMatchTheAuthoredFrames) {
    using eldenring::ow::owClipDurationFallback;
    EXPECT_NEAR(owClipDurationFallback(2), 28.0f / 31.071428f, 1e-5f);
    EXPECT_NEAR(owClipDurationFallback(3), 108.0f / 30.277779f, 1e-5f);
    EXPECT_NEAR(owClipDurationFallback(4), 26.0f / 31.153847f, 1e-5f);
    EXPECT_NEAR(owClipDurationFallback(5), 60.0f / 30.5f, 1e-5f);
    EXPECT_FLOAT_EQ(owClipDurationFallback(0), 0.0f);
}

TEST(OwViewmodelTest, MatrixLerpKeepsEndpoints) {
    eldenring::fp::M4 a;
    eldenring::fp::M4 b;
    b.m[11] = 4.0f;
    b = eldenring::fp::mul(b, eldenring::fp::rotY(0.6f));

    const eldenring::fp::M4 start = eldenring::ow::owLerpMatrix(a, b, 0.0f);
    const eldenring::fp::M4 end = eldenring::ow::owLerpMatrix(a, b, 1.0f);
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_NEAR(start.m[i], a.m[i], 1e-5f);
        EXPECT_NEAR(end.m[i], b.m[i], 1e-5f);
    }
}

TEST(OwViewmodelTest, ParsesTheShippedAssetWhenProvided) {
    std::string path;
#ifdef _WIN32
    char* raw = nullptr;
    size_t len = 0;
    if (_dupenv_s(&raw, &len, "MC_OW_ASSET") == 0 && raw != nullptr) {
        path = raw;
        std::free(raw);
    }
#else
    if (const char* env = std::getenv("MC_OW_ASSET")) path = env;
#endif
    if (path.empty()) GTEST_SKIP() << "set MC_OW_ASSET to the baked ow_gauntlet.bin";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << path;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    OwModel model;
    ASSERT_TRUE(parseOwModel(bytes.data(), bytes.size(), model)) << path;
    EXPECT_EQ(model.bones.size(), 36u);
    ASSERT_FALSE(model.submeshes.empty());
    EXPECT_TRUE(model.submeshes[0].skinned());
    EXPECT_EQ(model.clips.size(), 5u);
    for (const auto& [name, clip] : model.clips) {
        ASSERT_EQ(clip.tracks.size(), model.bones.size()) << name;
        size_t with_frames = 0;
        for (const auto& track : clip.tracks) {
            if (!track.empty()) {
                ++with_frames;
                EXPECT_EQ(track.size(), clip.frame_count) << name;
            }
        }
        EXPECT_EQ(with_frames, model.bones.size()) << name << ": a track name did not match the bone table";
    }
    auto bone_index = [&model](const char* name) {
        for (size_t i = 0; i < model.bones.size(); ++i) {
            if (model.bones[i].name == name) return i;
        }
        return model.bones.size();
    };
    const size_t wrist = bone_index("Wrist_R");
    ASSERT_LT(wrist, model.bones.size());
    const auto& idle_wrist = model.clips.at("idle").tracks[wrist];
    ASSERT_FALSE(idle_wrist.empty());
    // The palette is camera-relative metres, not UE centimetres.
    EXPECT_LT(std::abs(idle_wrist[0].m[11]), 5.0f);
    EXPECT_NEAR(model.clipDuration("punch"), 108.0f / 30.277779f, 1e-3f);
    EXPECT_NEAR(model.clipDuration("shoot"), 28.0f / 31.071428f, 1e-3f);
}

TEST(OwViewmodelTest, AnimatorBlendsAfterASeek) {
    OwModel model = makeAnimModel();
    OwAnimator anim;
    anim.setModel(&model);

    anim.tick(0.016f, 3, 0.0f); // punch frame 0, z = 10
    ASSERT_NEAR(anim.palette()[0].m[11], 10.0f, 1e-4f);

    // Releasing a charge seeks forward; the pose eases instead of popping.
    anim.tick(0.016f, 3, 2.0f); // starts the blend from the held windup pose
    EXPECT_NEAR(anim.palette()[0].m[11], 10.0f, 1e-4f);

    anim.tick(0.016f, 3, 2.0f); // punch frame 2, z = 12
    EXPECT_GT(anim.palette()[0].m[11], 10.0f);
    EXPECT_LT(anim.palette()[0].m[11], 12.0f);

    for (int i = 0; i < 10; ++i) anim.tick(0.016f, 3, 2.0f);
    EXPECT_NEAR(anim.palette()[0].m[11], 12.0f, 1e-4f);
}

TEST(OwViewmodelTest, ProjectionUsesItsOwnFov) {
    eldenring::ow::OwViewmodelConfig wide;
    wide.fov_deg = 90.f;
    eldenring::ow::OwViewmodelConfig narrow;
    narrow.fov_deg = 60.f;
    const mc::rig::Mat4 a = eldenring::ow::computeOwProjection(1920, 1080, wide);
    const mc::rig::Mat4 b = eldenring::ow::computeOwProjection(1920, 1080, narrow);
    EXPECT_NE(a.m[0], b.m[0]);
}

} // namespace
