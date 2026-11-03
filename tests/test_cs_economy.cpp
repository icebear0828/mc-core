#include <gtest/gtest.h>

#include "cs_ballistics.hpp"
#include "cs_economy.hpp"

using namespace eldenring::cs;

TEST(CsEconomy, TagCarriesTheArsenalSlot) {
    const uint32_t ak = csTagWithSlot(kCsShotTag, 1);
    EXPECT_EQ(csSlotFromTag(ak), 1u);
    EXPECT_NE(ak & kCsShotTag, 0u);

    const uint32_t knife = csTagWithSlot(kCsShotTag, 0);
    EXPECT_EQ(csSlotFromTag(knife), 0u);
    EXPECT_EQ(knife, kCsShotTag);
}

TEST(CsEconomy, KillRewardsSeparateGunAndKnife) {
    EXPECT_EQ(kCsStartingMoney, 800u);
    EXPECT_EQ(csKillReward(1), kCsGunKillReward);
    EXPECT_EQ(csKillReward(2), kCsGunKillReward);
    EXPECT_EQ(csKillReward(3), kCsGunKillReward);
    EXPECT_EQ(csKillReward(0), kCsKnifeKillReward);
    EXPECT_EQ(kCsKnifeKillReward, 1500u);
}

TEST(CsEconomy, OffersMatchTheArsenalSlots) {
    const CsAmmoOffer* ak = csAmmoOfferForSlot(1);
    ASSERT_NE(ak, nullptr);
    EXPECT_STREQ(ak->weapon_id, "weapon_ak47");
    EXPECT_STREQ(ak->name, "AK-47");
    EXPECT_EQ(ak->price, 200u);

    const CsAmmoOffer* awp = csAmmoOfferForKey(2);
    ASSERT_NE(awp, nullptr);
    EXPECT_EQ(awp->slot, 2u);
    EXPECT_EQ(awp->price, 250u);

    const CsAmmoOffer* m4 = csAmmoOfferForKey(3);
    ASSERT_NE(m4, nullptr);
    EXPECT_EQ(m4->slot, 3u);
    EXPECT_EQ(m4->price, 180u);

    EXPECT_EQ(csAmmoOfferForSlot(0), nullptr);
    EXPECT_EQ(csAmmoOfferForKey(4), nullptr);
}

TEST(CsEconomy, BuyAddsOneMagazine) {
    const CsPurchase p = csBuyMagazine(800, 10, 90, 30, 200);
    EXPECT_TRUE(p.bought);
    EXPECT_EQ(p.reserve, 40u);
    EXPECT_EQ(p.spent, 200u);
}

TEST(CsEconomy, BuyCapsAtReserveCapacity) {
    const CsPurchase p = csBuyMagazine(800, 80, 90, 30, 200);
    EXPECT_TRUE(p.bought);
    EXPECT_EQ(p.reserve, 90u);
    EXPECT_EQ(p.spent, 200u);
}

TEST(CsEconomy, BuyRefusesWhenReserveIsFull) {
    const CsPurchase p = csBuyMagazine(800, 90, 90, 30, 200);
    EXPECT_FALSE(p.bought);
    EXPECT_EQ(p.reserve, 90u);
    EXPECT_EQ(p.spent, 0u);
}

TEST(CsEconomy, BuyRefusesWhenMoneyIsShort) {
    const CsPurchase p = csBuyMagazine(199, 10, 90, 30, 200);
    EXPECT_FALSE(p.bought);
    EXPECT_EQ(p.reserve, 10u);
    EXPECT_EQ(p.spent, 0u);
}

TEST(CsEconomy, BuyRefusesWhenTheOfferIsUnknown) {
    EXPECT_EQ(csAmmoOfferForKey(0), nullptr);
    EXPECT_EQ(csAmmoOfferForKey(5), nullptr);
}
