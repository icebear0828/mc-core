#include <gtest/gtest.h>

#include "eldenring_shield.hpp"
#include "mc/inventory.hpp"

using namespace eldenring::shield;

TEST(EldenRingShield, AngleFacingForwardBlocksWithin100Degrees) {
    // Player facing along +Z (yaw = 0): forward is (0, 0, 1)
    const float player_pos[3] = {0.f, 0.f, 0.f};
    const float player_yaw = 0.f;

    // Attacker straight ahead (+Z) -> 0 degrees, inside 100 deg cone
    const float front_attacker[3] = {0.f, 0.f, 10.f};
    EXPECT_TRUE(isAttackBlocked(player_pos, player_yaw, front_attacker));

    // Attacker at 40 degrees forward-right -> cos(40 deg) ~ 0.766 >= cos(50 deg) ~ 0.642 -> blocked
    const float right_front[3] = {6.f, 0.f, 7.15f};
    EXPECT_TRUE(isAttackBlocked(player_pos, player_yaw, right_front));

    // Attacker at 40 degrees forward-left -> blocked
    const float left_front[3] = {-6.f, 0.f, 7.15f};
    EXPECT_TRUE(isAttackBlocked(player_pos, player_yaw, left_front));

    // Attacker at 60 degrees (outside 100 deg arc, cone half-angle is 50 deg) -> not blocked
    const float flank_attacker[3] = {8.66f, 0.f, 5.0f};
    EXPECT_FALSE(isAttackBlocked(player_pos, player_yaw, flank_attacker));

    // Attacker directly behind -> not blocked
    const float behind_attacker[3] = {0.f, 0.f, -5.f};
    EXPECT_FALSE(isAttackBlocked(player_pos, player_yaw, behind_attacker));
}

TEST(EldenRingShield, ShieldControllerRequiresShieldAndRightClick) {
    ShieldController s;
    EXPECT_FALSE(s.isBlocking());

    // Right held without shield -> not blocking
    s.update(0.1f, true, false);
    EXPECT_FALSE(s.isBlocking());

    // Right held with shield -> starts raise前摇 (5 ticks = 0.25s)
    s.update(0.1f, true, true);
    EXPECT_FALSE(s.isBlocking()); // 0.1s < 0.25s
    s.update(0.16f, true, true);  // total 0.26s >= 0.25s
    EXPECT_TRUE(s.isBlocking());

    // Release right click -> immediately lowers shield
    s.update(0.01f, false, true);
    EXPECT_FALSE(s.isBlocking());
}

TEST(EldenRingShield, OffhandSlotInInventoryHoldsShieldAndTotem) {
    mc::Inventory inv;
    EXPECT_TRUE(inv.offhand().empty());

    inv.setOffhand({mc::ItemId::Shield, 1});
    EXPECT_EQ(inv.offhand().item, mc::ItemId::Shield);
    EXPECT_EQ(inv.offhand().count, 1u);
    EXPECT_TRUE(inv.has(mc::ItemId::Shield));

    // Consuming offhand
    EXPECT_TRUE(inv.consumeOffhand());
    EXPECT_TRUE(inv.offhand().empty());
}

TEST(EldenRingShield, TotemInOffhandTriggersClamp) {
    mc::Inventory inv;
    inv.setOffhand({mc::ItemId::TotemOfUndying, 1});

    // Check if player holds totem either in main hand OR offhand
    EXPECT_TRUE(inv.holdsOrOffhand(mc::ItemId::TotemOfUndying, 0)); // slot 0 empty, but offhand has it!
}

TEST(EldenRingShield, ShieldBlocksOnlyFrontHitsWithKnownAttackerAndPositions) {
    const float player_pos[3] = {0.f, 0.f, 0.f};
    const float front[3] = {0.f, 0.f, 10.f};
    const float behind[3] = {0.f, 0.f, -10.f};
    // facing +Z: front is blocked, behind is not
    EXPECT_TRUE(shieldBlocksHit(true, true, player_pos, 0.f, front));
    EXPECT_FALSE(shieldBlocksHit(true, true, player_pos, 0.f, behind));
    // environmental damage (no attacker) is never blocked: Minecraft's shield does not stop falls, fire or poison
    EXPECT_FALSE(shieldBlocksHit(false, true, player_pos, 0.f, front));
    // unreadable positions must not turn into invulnerability
    EXPECT_FALSE(shieldBlocksHit(true, false, player_pos, 0.f, front));
}
