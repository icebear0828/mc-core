#include <gtest/gtest.h>

#include "cs_scope.hpp"
#include "cs_weapon_registry.hpp"

namespace {

using namespace eldenring::cs;

TEST(CsScopeTest, RightClickCyclesOffZoomOneZoomTwoOff) {
    EXPECT_EQ(nextScopeLevel(kScopeOff, true), kScopeLevel1);
    EXPECT_EQ(nextScopeLevel(kScopeLevel1, true), kScopeLevel2);
    EXPECT_EQ(nextScopeLevel(kScopeLevel2, true), kScopeOff);
}

TEST(CsScopeTest, WeaponsWithoutScopeStayOff) {
    EXPECT_EQ(nextScopeLevel(kScopeOff, false), kScopeOff);
    EXPECT_EQ(nextScopeLevel(kScopeLevel1, false), kScopeOff);
    EXPECT_EQ(nextScopeLevel(kScopeLevel2, false), kScopeOff);
}

TEST(CsScopeTest, ScopeActiveOnlyForTheTwoZoomStages) {
    EXPECT_FALSE(scopeActive(kScopeOff));
    EXPECT_TRUE(scopeActive(kScopeLevel1));
    EXPECT_TRUE(scopeActive(kScopeLevel2));
    EXPECT_FALSE(scopeActive(3));
}

TEST(CsScopeTest, ScopeFovFollowsTheZoomStage) {
    EXPECT_FLOAT_EQ(scopeFovDegrees(kScopeOff, 23.2f, 5.6f), 0.f);
    EXPECT_FLOAT_EQ(scopeFovDegrees(kScopeLevel1, 23.2f, 5.6f), 23.2f);
    EXPECT_FLOAT_EQ(scopeFovDegrees(kScopeLevel2, 23.2f, 5.6f), 5.6f);
}

TEST(CsScopeTest, ScopedSensitivityFollowsTheFovRatio) {
    // Native 48 deg vertical; zoom 1 24 deg -> half sensitivity, zoom 2 6 deg -> 1/8.
    EXPECT_FLOAT_EQ(scopeSensitivityScale(kScopeOff, 24.f, 6.f, 48.f, 1.f), 1.f);
    EXPECT_FLOAT_EQ(scopeSensitivityScale(kScopeLevel1, 24.f, 6.f, 48.f, 1.f), 0.5f);
    EXPECT_FLOAT_EQ(scopeSensitivityScale(kScopeLevel2, 24.f, 6.f, 48.f, 1.f), 0.125f);
    EXPECT_FLOAT_EQ(scopeSensitivityScale(kScopeLevel1, 24.f, 6.f, 48.f, 2.f), 1.f);
    EXPECT_FLOAT_EQ(scopeSensitivityScale(kScopeLevel1, 24.f, 6.f, 0.f, 1.f), 1.f);
}

TEST(CsScopeTest, ScopedShotRemembersTheStageToReopen) {
    // CS2 unscopes for the bolt cycle and re-zooms when the bolt is home.
    EXPECT_EQ(scopeResumeLevelAfterShot(kScopeLevel1), kScopeLevel1);
    EXPECT_EQ(scopeResumeLevelAfterShot(kScopeLevel2), kScopeLevel2);
    EXPECT_EQ(scopeResumeLevelAfterShot(kScopeOff), kScopeOff);
    EXPECT_EQ(scopeResumeLevelAfterShot(3), kScopeOff);
}

TEST(CsScopeTest, RegistryOnlyMarksTheAwpAsScoped) {
    const auto& reg = CSWeaponRegistry::instance();
    const auto* awp = reg.findWeapon("weapon_awp");
    ASSERT_NE(awp, nullptr);
    EXPECT_TRUE(awp->ballistics.has_scope);

    const auto* ak = reg.findWeapon("weapon_ak47");
    ASSERT_NE(ak, nullptr);
    EXPECT_FALSE(ak->ballistics.has_scope);

    const auto* m4 = reg.findWeapon("weapon_m4a1_silencer");
    ASSERT_NE(m4, nullptr);
    EXPECT_FALSE(m4->ballistics.has_scope);

    const auto* knife = reg.findWeapon("weapon_knife_butterfly");
    ASSERT_NE(knife, nullptr);
    EXPECT_FALSE(knife->ballistics.has_scope);
}

} // namespace
