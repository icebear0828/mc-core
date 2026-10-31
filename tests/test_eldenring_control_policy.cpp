#include <gtest/gtest.h>

#include "eldenring_control_policy.hpp"
#include "eldenring_flight.hpp"
#include "eldenring_roll.hpp"

#include <array>
#include <iterator>

namespace {

using eldenring::control::buildControlPlan;
using eldenring::control::ModeInputs;

bool masksKey(const mc::ControlPlan& plan, int key) {
    for (size_t i = 0; i < plan.masked_key_count; ++i) {
        if (plan.masked_keys[i] == key) return true;
    }
    return false;
}

TEST(EldenRingControlPolicy, DefaultPlanIsEmpty) {
    const mc::ControlPlan plan = buildControlPlan(ModeInputs{});
    EXPECT_TRUE(plan.isDefault());
}

TEST(EldenRingControlPolicy, InventoryOwnsMouseMotionKeyboardAndCursor) {
    ModeInputs in;
    in.inventory_open = true;

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseMotion));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Keyboard));
    EXPECT_TRUE(plan.owns(mc::ControlDomain::Cursor));
}

TEST(EldenRingControlPolicy, FirstPersonOwnsTheCamera) {
    ModeInputs in;
    in.first_person = true;

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.owns(mc::ControlDomain::Camera));
    EXPECT_EQ(plan.authorityFor(mc::ControlDomain::Movement), mc::ControlAuthority::Shared);
}

TEST(EldenRingControlPolicy, McCombatOwnsTheMouseOnlyWhenNativeInputIsAllowed) {
    ModeInputs in;
    in.mc_mode = true;
    in.native_input_allowed = false;
    EXPECT_FALSE(buildControlPlan(in).blocksNative(mc::ControlDomain::MouseButtons));

    in.native_input_allowed = true;
    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseButtons));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Attack));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Use));
    EXPECT_EQ(plan.authorityFor(mc::ControlDomain::Movement), mc::ControlAuthority::Shared);
}

TEST(EldenRingControlPolicy, McJumpMasksItsScanCode) {
    ModeInputs in;
    in.mc_mode = true;
    in.mc_jump = true;
    in.mc_jump_scan = 0x39; // DIK_SPACE

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(masksKey(plan, 0x39));
}

TEST(EldenRingControlPolicy, FirstPersonWithMcJumpFreezesTheJumpDomain) {
    ModeInputs in;
    in.first_person = true;
    in.mc_mode = true;
    in.mc_jump = true;
    in.mc_jump_scan = 0x39;

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Jump));
    EXPECT_TRUE(masksKey(plan, 0x39));
}

TEST(EldenRingControlPolicy, McJumpFreezesTheJumpDomainWithoutFirstPerson) {
    ModeInputs in;
    in.mc_mode = true;
    in.mc_jump = true;
    in.mc_jump_scan = 0x39;

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Jump));
    EXPECT_TRUE(masksKey(plan, 0x39));
}

TEST(EldenRingControlPolicy, VanillaRollKeysAreMasked) {
    ModeInputs in;
    in.mc_mode = true;
    in.disable_vanilla_roll = true;
    const eldenring::roll::RollMaskResult roll = eldenring::roll::computeRollMask(true, true, eldenring::roll::kVkShift);
    in.roll_keys = std::span<const int>(roll.scancodes, roll.count);

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Roll));
    EXPECT_TRUE(masksKey(plan, eldenring::roll::kDikLShift));
    EXPECT_TRUE(masksKey(plan, eldenring::roll::kDikRShift));
}

TEST(EldenRingControlPolicy, CreativeFlightMasksItsMovementKeys) {
    ModeInputs in;
    in.mc_mode = true;
    in.flying = true;
    in.flight_keys = std::span<const int>(eldenring::flight::kHiddenKeys, std::size(eldenring::flight::kHiddenKeys));

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.owns(mc::ControlDomain::Movement));
    EXPECT_TRUE(plan.owns(mc::ControlDomain::Jump));
    for (const int key : eldenring::flight::kHiddenKeys) EXPECT_TRUE(masksKey(plan, key));
}

TEST(EldenRingControlPolicy, CsModeMasksItsActionKeysAndWheel) {
    ModeInputs in;
    in.cs_mode = true;
    in.foreground = true;
    const std::array<int, 6> cs_keys{0x13, 0x21, 0x02, 0x03, 0x04, 0x05};
    in.cs_keys = std::span<const int>(cs_keys);

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseWheel));
    for (const int key : cs_keys) EXPECT_TRUE(masksKey(plan, key));
}

TEST(EldenRingControlPolicy, InventoryKeepsCsKeysVisibleToTheGame) {
    ModeInputs in;
    in.cs_mode = true;
    in.inventory_open = true;
    const std::array<int, 1> cs_keys{0x21};
    in.cs_keys = std::span<const int>(cs_keys);

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_FALSE(masksKey(plan, 0x21));
    // The wheel is still owned by CS mode; the inventory controller handles the rest.
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseWheel));
}

TEST(EldenRingControlPolicy, BackgroundDoesNotKeepForegroundOnlyMasks) {
    ModeInputs in;
    in.mc_mode = true;
    in.foreground = false;
    in.mc_jump = true;
    in.mc_jump_scan = 0x39;

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_FALSE(masksKey(plan, 0x39));
}

TEST(EldenRingControlPolicy, FirstPersonAndCsModeCompose) {
    ModeInputs in;
    in.first_person = true;
    in.cs_mode = true;
    in.native_input_allowed = true;
    const std::array<int, 1> cs_keys{0x21};
    in.cs_keys = std::span<const int>(cs_keys);

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.owns(mc::ControlDomain::Camera));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseButtons));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseWheel));
    EXPECT_TRUE(masksKey(plan, 0x21));
}

TEST(EldenRingControlPolicy, OwModeMasksActionKeysAndOwnsCombat) {
    ModeInputs in;
    in.ow_mode = true;
    in.foreground = true;
    in.native_input_allowed = true;
    // OW action keys: Shift (Seismic Slam), E (Rising Uppercut), Q (Meteor Strike), F7 (1P/3P toggle)
    const std::array<int, 4> ow_keys{0x2A /*LSHIFT*/, 0x12 /*E*/, 0x10 /*Q*/, 0x41 /*F7*/};
    in.ow_keys = std::span<const int>(ow_keys);

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseButtons));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseWheel));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Attack));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Use));
    for (const int key : ow_keys) {
        EXPECT_TRUE(masksKey(plan, key));
    }
}

TEST(EldenRingControlPolicy, OwModeStrictlyIsolatesFromMcAndCs) {
    ModeInputs in;
    // Attempt multi-mode collision: OW mode + MC mode + CS mode simultaneously
    in.ow_mode = true;
    in.mc_mode = true;
    in.cs_mode = true;
    in.foreground = true;
    in.native_input_allowed = true;
    in.flying = true;
    in.flight_keys = std::span<const int>(eldenring::flight::kHiddenKeys, std::size(eldenring::flight::kHiddenKeys));
    const std::array<int, 1> cs_keys{0x21};
    in.cs_keys = std::span<const int>(cs_keys);

    const mc::ControlPlan plan = buildControlPlan(in);
    // Under strict isolation: OW mode takes exclusive authority, suppressing MC flight and CS keys.
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::MouseWheel));
    EXPECT_FALSE(masksKey(plan, 0x21)); // CS key must NOT be masked
    for (const int key : eldenring::flight::kHiddenKeys) {
        EXPECT_FALSE(masksKey(plan, key)); // MC flight keys must NOT be masked
    }
}

} // namespace
