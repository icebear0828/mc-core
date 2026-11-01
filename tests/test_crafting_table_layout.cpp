#include <gtest/gtest.h>

#include "mc/crafting_table_layout.hpp"

using namespace mc;

TEST(CraftingTableLayout, PanelIsCentredAndFollowsTheGuiScale) {
    CraftingTableLayout l(1920.f, 1080.f);
    EXPECT_EQ(l.scale(), 3);
    const HudRect p = l.panel();
    EXPECT_FLOAT_EQ(p.w, 176.f * 3);
    EXPECT_FLOAT_EQ(p.h, 166.f * 3);
    EXPECT_NEAR(p.x + p.w / 2, 960.f, 1.5f);
    EXPECT_NEAR(p.y + p.h / 2, 540.f, 1.5f);
}

TEST(CraftingTableLayout, GridAndResultFollowTheVanillaCraftingTable) {
    CraftingTableLayout l(1920.f, 1080.f);
    const HudRect p = l.panel();
    const float s = static_cast<float>(l.scale());
    for (int i = 0; i < CraftingTableLayout::kGridSlots; ++i) {
        const HudRect r = l.gridSlot(i);
        EXPECT_FLOAT_EQ(r.x, p.x + (30 + 18 * (i % 3)) * s);
        EXPECT_FLOAT_EQ(r.y, p.y + (17 + 18 * (i / 3)) * s);
        EXPECT_FLOAT_EQ(r.w, 16.f * s);
    }
    const HudRect out = l.resultSlot();
    EXPECT_FLOAT_EQ(out.x, p.x + 124.f * s);
    EXPECT_FLOAT_EQ(out.y, p.y + 35.f * s);
    EXPECT_FLOAT_EQ(l.invSlot(0).y, p.y + 142.f * s);
    EXPECT_FLOAT_EQ(l.invSlot(9).y, p.y + 84.f * s);
}

TEST(CraftingTableLayout, HitTestFindsGridResultAndInventory) {
    CraftingTableLayout l(1920.f, 1080.f);
    SlotRef r = l.hitTest(l.gridSlot(4).x + 2, l.gridSlot(4).y + 2);
    EXPECT_EQ(r.kind, SlotRef::Kind::Crafting);
    EXPECT_EQ(r.index, 4);
    r = l.hitTest(l.resultSlot().x + 2, l.resultSlot().y + 2);
    EXPECT_EQ(r.kind, SlotRef::Kind::CraftResult);
    r = l.hitTest(l.invSlot(17).x + 2, l.invSlot(17).y + 2);
    EXPECT_EQ(r.kind, SlotRef::Kind::Inventory);
    EXPECT_EQ(r.index, 17);
    EXPECT_EQ(l.hitTest(1.f, 1.f).kind, SlotRef::Kind::None);
    const HudRect p = l.panel();
    EXPECT_EQ(l.hitTest(p.x + 2, p.y + 2).kind, SlotRef::Kind::Panel);
}
