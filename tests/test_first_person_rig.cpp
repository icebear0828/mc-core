#include <gtest/gtest.h>

#include "mc/rig/first_person_rig.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace {

// Records every call the rig makes and models the host camera: save() captures the native eye/FOV and restore()
// puts them back, so a test can check the round trip as well as the call ordering.
class FakeFirstPersonCamera final : public mc::IFirstPersonCamera {
public:
    bool save() override {
        calls.emplace_back("save");
        ++saves;
        if (!can_save) return false;
        for (int i = 0; i < 3; ++i) saved_eye_[i] = eye[i];
        saved_fov_ = fov;
        return true;
    }

    bool writeEye(const float e[3]) override {
        calls.emplace_back("eye");
        ++eye_writes;
        for (int i = 0; i < 3; ++i) {
            last_eye[i] = e[i];
            eye[i] = e[i];
        }
        return true;
    }

    bool writeFov(float fov_y) override {
        calls.emplace_back("fov");
        ++fov_writes;
        fov = fov_y;
        return true;
    }

    void restore() override {
        calls.emplace_back("restore");
        ++restores;
        for (int i = 0; i < 3; ++i) eye[i] = saved_eye_[i];
        fov = saved_fov_;
    }

    void hideNativeHead(bool hidden) override {
        calls.emplace_back(hidden ? "hide" : "show");
        ++head_calls;
        head_hidden = hidden;
    }

    bool can_save{true};
    float eye[3]{1.f, 2.f, 3.f}; // the game's native camera position
    float fov{0.8f};             // the game's native vertical FOV, radians
    int saves{0};
    int eye_writes{0};
    int fov_writes{0};
    int restores{0};
    int head_calls{0};
    bool head_hidden{false};
    float last_eye[3]{};
    std::vector<std::string> calls;

private:
    float saved_eye_[3]{};
    float saved_fov_{0.f};
};

const float kFeet[3] = {10.f, 20.f, 30.f};

} // namespace

TEST(FirstPersonRig, EnterSavesHidesTheHeadAndWritesTheEyeOnce) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);

    ASSERT_TRUE(rig.enter(kFeet));

    EXPECT_TRUE(rig.active());
    EXPECT_TRUE(rig.hideHead());
    EXPECT_TRUE(camera.head_hidden);
    EXPECT_EQ(camera.saves, 1);
    EXPECT_EQ(camera.eye_writes, 1);
    EXPECT_EQ(camera.restores, 0);
    EXPECT_FLOAT_EQ(camera.last_eye[0], kFeet[0]);
    EXPECT_FLOAT_EQ(camera.last_eye[1], kFeet[1] + mc::rig::FirstPersonRig::kStandingEyeHeight);
    EXPECT_FLOAT_EQ(camera.last_eye[2], kFeet[2]);
}

TEST(FirstPersonRig, ExitRestoresTheNativeCameraAndShowsTheHeadOnce) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);
    ASSERT_TRUE(rig.enter(kFeet));

    rig.exit();

    EXPECT_FALSE(rig.active());
    EXPECT_FALSE(rig.hideHead());
    EXPECT_FALSE(camera.head_hidden);
    EXPECT_EQ(camera.restores, 1);
    EXPECT_EQ(camera.head_calls, 2); // hide on enter, show on exit
    EXPECT_FLOAT_EQ(camera.eye[0], 1.f); // the native position is back
    EXPECT_FLOAT_EQ(camera.eye[1], 2.f);
    EXPECT_FLOAT_EQ(camera.eye[2], 3.f);

    rig.exit(); // idempotent: a second exit does not restore twice
    EXPECT_EQ(camera.restores, 1);
}

TEST(FirstPersonRig, EnterWritesTheCameraBeforeTheHeadIsShownOnExit) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);

    ASSERT_TRUE(rig.enter(kFeet));
    rig.exit();

    const std::vector<std::string> expected{"save", "hide", "eye", "restore", "show"};
    EXPECT_EQ(camera.calls, expected);
}

TEST(FirstPersonRig, InactiveRigWritesNothing) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);

    EXPECT_FALSE(rig.writeEye(kFeet));
    EXPECT_FALSE(rig.writeFov(1.f));
    EXPECT_FALSE(rig.active());
    EXPECT_EQ(camera.eye_writes, 0);
    EXPECT_EQ(camera.fov_writes, 0);
    EXPECT_EQ(camera.saves, 0);
    EXPECT_EQ(camera.head_calls, 0);
}

TEST(FirstPersonRig, EnterStaysInactiveWhenTheHostCameraCannotBeSaved) {
    FakeFirstPersonCamera camera;
    camera.can_save = false;
    mc::rig::FirstPersonRig rig(camera);

    EXPECT_FALSE(rig.enter(kFeet));
    EXPECT_FALSE(rig.active());
    EXPECT_EQ(camera.saves, 1);
    EXPECT_EQ(camera.eye_writes, 0);
    EXPECT_EQ(camera.head_calls, 0);
}

TEST(FirstPersonRig, EnteringTwiceDoesNotWriteOrSaveAgain) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);

    ASSERT_TRUE(rig.enter(kFeet));
    ASSERT_TRUE(rig.enter(kFeet));

    EXPECT_EQ(camera.saves, 1);
    EXPECT_EQ(camera.eye_writes, 1);
    EXPECT_EQ(camera.head_calls, 1);
}

TEST(FirstPersonRig, EyeSitsAboveTheFeetByTheEyeHeight) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);
    rig.setEyeHeight(1.5f);
    ASSERT_TRUE(rig.enter(kFeet));

    float eye[3];
    mc::rig::FirstPersonRig::eyeFromFeet(kFeet, 1.5f, eye);
    EXPECT_FLOAT_EQ(eye[0], kFeet[0]);
    EXPECT_FLOAT_EQ(eye[1], kFeet[1] + 1.5f);
    EXPECT_FLOAT_EQ(eye[2], kFeet[2]);

    ASSERT_TRUE(rig.writeEye(kFeet));
    EXPECT_FLOAT_EQ(camera.last_eye[1], kFeet[1] + 1.5f);
}

TEST(FirstPersonRig, EyeHeightConvergesToTheSneakingTarget) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);
    ASSERT_TRUE(rig.enter(kFeet));

    for (int i = 0; i < 240; ++i) rig.stepEyeHeight(true, 1.f / 60.f);

    EXPECT_NEAR(rig.eyeHeight(), mc::rig::FirstPersonRig::kSneakingEyeHeight, 1e-4f);
    ASSERT_TRUE(rig.writeEye(kFeet));
    EXPECT_NEAR(camera.last_eye[1], kFeet[1] + mc::rig::FirstPersonRig::kSneakingEyeHeight, 1e-4f);
}

TEST(FirstPersonRig, EyeHeightConvergesBackToTheStandingTarget) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);
    ASSERT_TRUE(rig.enter(kFeet));
    rig.setEyeHeight(mc::rig::FirstPersonRig::kSneakingEyeHeight);

    for (int i = 0; i < 240; ++i) rig.stepEyeHeight(false, 1.f / 60.f);

    EXPECT_NEAR(rig.eyeHeight(), mc::rig::FirstPersonRig::kStandingEyeHeight, 1e-4f);
}

TEST(FirstPersonRig, EyeHeightIsFrameRateIndependent) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);
    ASSERT_TRUE(rig.enter(kFeet));

    for (int i = 0; i < 3; ++i) rig.stepEyeHeight(true, 1.f / 30.f);
    const float at30 = rig.eyeHeight();

    rig.setEyeHeight(mc::rig::FirstPersonRig::kStandingEyeHeight);
    for (int i = 0; i < 12; ++i) rig.stepEyeHeight(true, 1.f / 120.f);
    const float at120 = rig.eyeHeight();

    EXPECT_NEAR(at30, at120, 1e-4f);
}

TEST(FirstPersonRig, EyeHeightDoesNotMoveWhileInactiveOrWithoutTime) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);

    rig.stepEyeHeight(true, 1.f / 60.f);
    EXPECT_FLOAT_EQ(rig.eyeHeight(), mc::rig::FirstPersonRig::kStandingEyeHeight);

    ASSERT_TRUE(rig.enter(kFeet));
    rig.stepEyeHeight(true, 0.f);
    rig.stepEyeHeight(true, -1.f);
    EXPECT_FLOAT_EQ(rig.eyeHeight(), mc::rig::FirstPersonRig::kStandingEyeHeight);
}

TEST(FirstPersonRig, FovIsWrittenWhileActiveAndRestoredOnExit) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);
    const float native_fov = camera.fov;
    ASSERT_TRUE(rig.enter(kFeet));

    ASSERT_TRUE(rig.writeFov(1.2f));
    EXPECT_FLOAT_EQ(camera.fov, 1.2f);
    EXPECT_TRUE(rig.fovWritten());

    rig.exit();
    EXPECT_FLOAT_EQ(camera.fov, native_fov);
    EXPECT_FALSE(rig.fovWritten());
}

TEST(FirstPersonRig, InactiveRigDoesNotWriteFov) {
    FakeFirstPersonCamera camera;
    mc::rig::FirstPersonRig rig(camera);
    const float native_fov = camera.fov;

    EXPECT_FALSE(rig.writeFov(1.2f));
    EXPECT_FLOAT_EQ(camera.fov, native_fov);
    EXPECT_EQ(camera.fov_writes, 0);
}
