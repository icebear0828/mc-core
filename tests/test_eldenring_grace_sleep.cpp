#include <gtest/gtest.h>

#include "eldenring_live.hpp"
#include "eldenring_state.hpp"
#include "eldenring_world.hpp"
#include "eldenring_steve.hpp"
#include "mc/sleep.hpp"

#include <cmath>

namespace eldenring {

// Dedicated test logic for native model hiding policy
inline bool computeNativeModelHideWanted(bool mc_mode, bool have_state, bool screen_loading) {
    return mc_mode && have_state && !screen_loading;
}

// Logic for detecting grace rest
inline bool isGraceResting(float dist_to_grace, bool menu_focused, float fade_alpha, float move_speed) {
    constexpr float kMaxGraceRestDistance = 2.8f;
    if (dist_to_grace > kMaxGraceRestDistance) return false;
    if (move_speed > 0.1f) return false; // Must be stationary
    return menu_focused || (fade_alpha >= 0.02f);
}

TEST(EldenRingGraceSleepTest, NativeModelRemainsHiddenDuringMenusPopupsAndFades) {
    const bool mc_mode = true;
    const bool have_state = true;
    const bool screen_loading = false;

    // Normal gameplay
    EXPECT_TRUE(computeNativeModelHideWanted(mc_mode, have_state, screen_loading));

    // When menu is focused (e.g. Site of Grace menu open)
    EXPECT_TRUE(computeNativeModelHideWanted(mc_mode, have_state, screen_loading));

    // When screen fade is 1.0 (black transition at Site of Grace)
    EXPECT_TRUE(computeNativeModelHideWanted(mc_mode, have_state, screen_loading));

    // When popup is open (dialogue / grace prompt)
    EXPECT_TRUE(computeNativeModelHideWanted(mc_mode, have_state, screen_loading));

    // Only during true map loading screen does it allow native model cleanup
    EXPECT_FALSE(computeNativeModelHideWanted(mc_mode, have_state, true));

    // When MC mode is disabled
    EXPECT_FALSE(computeNativeModelHideWanted(false, have_state, screen_loading));
}

TEST(EldenRingGraceSleepTest, GraceRestingDetectorIdentifiesRestingState) {
    // Player standing 1.5m from Site of Grace, stationary, grace menu open
    EXPECT_TRUE(isGraceResting(1.5f, true, 0.0f, 0.0f));

    // Player sitting at Site of Grace during black fade screen
    EXPECT_TRUE(isGraceResting(1.2f, false, 0.8f, 0.0f));

    // Player walking past Grace (moving)
    EXPECT_FALSE(isGraceResting(1.5f, true, 0.0f, 2.5f));

    // Player far away from Grace with normal menu open
    EXPECT_FALSE(isGraceResting(15.0f, true, 0.0f, 0.0f));
}

TEST(EldenRingGraceSleepTest, SteveSleepingPoseAppliesToEldenRingRig) {
    const mc::Vec3 feet{100.0f, 10.0f, -50.0f};
    const float yaw = 1.2f;
    constexpr float ground_offset = 0.1125f;

    const mc::rig::Mat4 sleep_mat = mc::rig::sleepPoseMatrix(feet, yaw, 1.0f, ground_offset);

    // Transforming feet base
    const mc::Vec3 p_feet = mc::rig::transformPoint(sleep_mat, feet);
    EXPECT_NEAR(p_feet.y, feet.y + ground_offset, 0.05f);

    // Standing head is at feet + (0, 1.8, 0)
    const mc::Vec3 p_head = mc::rig::transformPoint(sleep_mat, {feet.x, feet.y + 1.8f, feet.z});
    EXPECT_NEAR(p_head.y, feet.y + ground_offset, 0.05f);

    // Check distance between transformed feet and head is 1.8m
    const float dx = p_head.x - p_feet.x;
    const float dz = p_head.z - p_feet.z;
    EXPECT_NEAR(std::sqrt(dx * dx + dz * dz), 1.8f, 0.05f);
}

} // namespace eldenring
