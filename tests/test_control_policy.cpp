#include <gtest/gtest.h>

#include "mc/control_policy.hpp"

#include <array>

namespace {

using mc::ControlAuthority;
using mc::ControlDomain;
using mc::ControlPlan;
using mc::ControlRequest;
using mc::ControlResolver;

TEST(ControlPolicy, DefaultPlanLeavesEverythingNative) {
    const ControlPlan plan = ControlResolver{}.resolve();

    EXPECT_TRUE(plan.isDefault());
    EXPECT_EQ(plan.authorityFor(ControlDomain::Camera), ControlAuthority::Native);
    EXPECT_EQ(plan.authorityFor(ControlDomain::Movement), ControlAuthority::Native);
    EXPECT_EQ(plan.masked_key_count, 0u);
    EXPECT_FALSE(plan.blocksNative(ControlDomain::MouseButtons));
    EXPECT_FALSE(plan.owns(ControlDomain::Camera));
}

TEST(ControlPolicy, ModAndNoneBlockTheHostButSharedDoesNot) {
    ControlResolver resolver;
    resolver.request(ControlDomain::Camera, ControlAuthority::Mod, 10);
    resolver.request(ControlDomain::Movement, ControlAuthority::Shared, 10);
    resolver.request(ControlDomain::Jump, ControlAuthority::None, 10);

    const ControlPlan plan = resolver.resolve();
    EXPECT_TRUE(plan.blocksNative(ControlDomain::Camera));
    EXPECT_TRUE(plan.owns(ControlDomain::Camera));
    EXPECT_FALSE(plan.blocksNative(ControlDomain::Movement));
    EXPECT_FALSE(plan.owns(ControlDomain::Movement));
    EXPECT_TRUE(plan.blocksNative(ControlDomain::Jump));
    EXPECT_FALSE(plan.owns(ControlDomain::Jump));
}

TEST(ControlPolicy, HigherPriorityWinsRegardlessOfRegistrationOrder) {
    ControlResolver resolver;
    resolver.request(ControlDomain::Camera, ControlAuthority::Native, 10);
    resolver.request(ControlDomain::Camera, ControlAuthority::Mod, 50);
    resolver.request(ControlDomain::Camera, ControlAuthority::Shared, 20);

    EXPECT_EQ(resolver.resolve().authorityFor(ControlDomain::Camera), ControlAuthority::Mod);
}

TEST(ControlPolicy, LaterRequestWinsOnEqualPriority) {
    ControlResolver resolver;
    resolver.request(ControlDomain::Camera, ControlAuthority::Mod, 10);
    resolver.request(ControlDomain::Camera, ControlAuthority::Native, 10);

    EXPECT_EQ(resolver.resolve().authorityFor(ControlDomain::Camera), ControlAuthority::Native);
}

TEST(ControlPolicy, KeyRequestsAreDeduplicatedAndCarriedIntoThePlan) {
    ControlResolver resolver;
    resolver.request(ControlDomain::Key, ControlAuthority::Mod, 10, 0x39);
    resolver.request(ControlDomain::Key, ControlAuthority::Mod, 10, 0x39); // duplicate
    resolver.request(ControlDomain::Key, ControlAuthority::Mod, 10, 0x2A);

    const ControlPlan plan = resolver.resolve();
    ASSERT_EQ(plan.masked_key_count, 2u);
    EXPECT_EQ(plan.masked_keys[0], 0x39);
    EXPECT_EQ(plan.masked_keys[1], 0x2A);
}

TEST(ControlPolicy, AKeyMaskCanBeReleasedByAHigherPriorityRequest) {
    ControlResolver resolver;
    resolver.request(ControlDomain::Key, ControlAuthority::Mod, 10, 0x39);
    resolver.request(ControlDomain::Key, ControlAuthority::Native, 50, 0x39);

    const ControlPlan plan = resolver.resolve();
    EXPECT_EQ(plan.masked_key_count, 0u);
    EXPECT_TRUE(plan.isDefault());
}

TEST(ControlPolicy, NonPositiveKeyIdsAreIgnored) {
    ControlResolver resolver;
    resolver.request(ControlDomain::Key, ControlAuthority::Mod, 10, 0);
    resolver.request(ControlDomain::Key, ControlAuthority::Mod, 10, -1);

    EXPECT_EQ(resolver.resolve().masked_key_count, 0u);
}

TEST(ControlPolicy, PackedAuthoritiesRoundTrip) {
    ControlResolver resolver;
    resolver.request(ControlDomain::Camera, ControlAuthority::Mod, 10);
    resolver.request(ControlDomain::Movement, ControlAuthority::Shared, 10);
    resolver.request(ControlDomain::Jump, ControlAuthority::None, 10);
    const ControlPlan plan = resolver.resolve();

    const uint64_t bits = plan.packedAuthorities();
    EXPECT_EQ(mc::controlAuthorityFromPacked(bits, ControlDomain::Camera), ControlAuthority::Mod);
    EXPECT_EQ(mc::controlAuthorityFromPacked(bits, ControlDomain::Movement), ControlAuthority::Shared);
    EXPECT_EQ(mc::controlAuthorityFromPacked(bits, ControlDomain::Jump), ControlAuthority::None);
    EXPECT_EQ(mc::controlAuthorityFromPacked(bits, ControlDomain::Attack), ControlAuthority::Native);
}

TEST(ControlPolicy, CommonHelpersCompose) {
    ControlResolver resolver;
    mc::requestMinecraftCombat(resolver, 100);
    mc::requestFirstPerson(resolver, 90);
    mc::requestInventory(resolver, 80);

    const ControlPlan plan = resolver.resolve();
    EXPECT_TRUE(plan.blocksNative(ControlDomain::MouseButtons));
    EXPECT_TRUE(plan.blocksNative(ControlDomain::Attack));
    EXPECT_TRUE(plan.blocksNative(ControlDomain::Use));
    EXPECT_TRUE(plan.owns(ControlDomain::Camera));
    EXPECT_EQ(plan.authorityFor(ControlDomain::Movement), ControlAuthority::Shared);
    EXPECT_TRUE(plan.blocksNative(ControlDomain::MouseMotion));
    EXPECT_TRUE(plan.blocksNative(ControlDomain::Keyboard));
    EXPECT_TRUE(plan.owns(ControlDomain::Cursor));
}

TEST(ControlPolicy, RequestCapacityIsBounded) {
    ControlResolver resolver;
    for (size_t i = 0; i < mc::kMaxControlRequests + 10; ++i) {
        resolver.request(ControlDomain::Key, ControlAuthority::Mod, 1, static_cast<int>(i) + 1);
    }
    EXPECT_EQ(resolver.requestCount(), mc::kMaxControlRequests);
    EXPECT_EQ(resolver.resolve().masked_key_count, mc::kMaxMaskedKeys);
}

} // namespace
