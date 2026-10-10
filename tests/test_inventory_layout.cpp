#include <gtest/gtest.h>

#include "mc/inventory_layout.hpp"

using namespace mc;

TEST(InventoryLayout, PanelIsCentredAndAnIntegerMultipleOfTheGuiScale) {
    InventoryLayout l(1920.f, 1080.f);
    EXPECT_EQ(l.scale(), 3);
    const HudRect p = l.panel();
    EXPECT_FLOAT_EQ(p.w, 176.f * 3);
    EXPECT_FLOAT_EQ(p.h, 167.f * 3);
    EXPECT_NEAR(p.x + p.w / 2, 960.f, 1.5f);
    EXPECT_NEAR(p.y + p.h / 2, 540.f, 1.5f);
}

TEST(InventoryLayout, ScaleFollowsTheHudScaleAndNeverOverflowsTheScreen) {
    InventoryLayout small(1280.f, 720.f);
    EXPECT_EQ(small.scale(), 2);
    InventoryLayout huge(7680.f, 3000.f); // the HUD scale alone would be 8; 167 * 8 = 1336 still fits
    EXPECT_LE(huge.panel().h, 3000.f * 0.95f);
    InventoryLayout tiny(300.f, 150.f);
    EXPECT_EQ(tiny.scale(), 1);
}

TEST(InventoryLayout, SlotsFollowTheContainerTexture) {
    InventoryLayout l(1920.f, 1080.f);
    const HudRect p = l.panel();
    const HudRect first_main = l.invSlot(9);
    EXPECT_FLOAT_EQ(first_main.x, p.x + 8 * 3);
    EXPECT_FLOAT_EQ(first_main.y, p.y + 85 * 3);
    EXPECT_FLOAT_EQ(first_main.w, 16.f * 3);
    const HudRect hot = l.invSlot(0);
    EXPECT_FLOAT_EQ(hot.y, p.y + 143 * 3);
    EXPECT_FLOAT_EQ(l.invSlot(4).x, p.x + (8 + 18 * 4) * 3);
    EXPECT_FLOAT_EQ(l.invSlot(10).y, first_main.y); // same row
    EXPECT_FLOAT_EQ(l.invSlot(18).y, first_main.y + 18 * 3);
    const HudRect pal = l.paletteSlot(10); // row 1, column 1
    EXPECT_FLOAT_EQ(pal.x, p.x + (8 + 18) * 3);
    EXPECT_FLOAT_EQ(pal.y, p.y + (18 + 18) * 3);
}

TEST(InventoryLayout, HitTestFindsSlotsAndMissesTheGaps) {
    InventoryLayout l(1920.f, 1080.f);
    const HudRect s = l.invSlot(13);
    SlotRef r = l.hitTest(s.x + 5, s.y + 5);
    EXPECT_EQ(r.kind, SlotRef::Kind::Inventory);
    EXPECT_EQ(r.index, 13);
    r = l.hitTest(l.paletteSlot(2).x + 1, l.paletteSlot(2).y + 1);
    EXPECT_EQ(r.kind, SlotRef::Kind::Palette);
    EXPECT_EQ(r.index, 2);
    r = l.hitTest(l.invSlot(0).x + 1, l.invSlot(0).y + 1);
    EXPECT_EQ(r.kind, SlotRef::Kind::Inventory);
    EXPECT_EQ(r.index, 0);
    // the 2 px between two slot icons belongs to the slot frame, not to the neighbour: still the frame of one of them
    EXPECT_EQ(l.hitTest(1.f, 1.f).kind, SlotRef::Kind::None);
    const HudRect p = l.panel();
    EXPECT_EQ(l.hitTest(p.x + 2.f, p.y + 2.f).kind, SlotRef::Kind::Panel); // inside the panel, not on a slot
    EXPECT_EQ(l.hitTest(p.x + p.w + 10.f, p.y).kind, SlotRef::Kind::None);
}

TEST(InventoryLayout, PaletteListsEveryItemOnce) {
    const auto& items = paletteItems();
    EXPECT_EQ(items.size(), 17u);
    EXPECT_EQ(items.front(), ItemId::DiamondSword);
    for (size_t i = 0; i < items.size(); ++i) {
        EXPECT_NE(items[i], ItemId::None);
        for (size_t j = i + 1; j < items.size(); ++j) EXPECT_NE(items[i], items[j]);
    }
    EXPECT_LE(items.size(), static_cast<size_t>(InventoryLayout::kPaletteSlots));
}

TEST(InventoryLayout, WithoutThePaletteItsSlotsAreOnlyPanel) {
    const mc::InventoryLayout on(1920.f, 1080.f);
    const mc::InventoryLayout off(1920.f, 1080.f, false);
    const mc::HudRect slot = on.paletteSlot(3);
    const float x = slot.x + slot.w * 0.5f, y = slot.y + slot.h * 0.5f;
    EXPECT_EQ(on.hitTest(x, y).kind, mc::SlotRef::Kind::Palette);
    EXPECT_EQ(off.hitTest(x, y).kind, mc::SlotRef::Kind::Panel);
    // the player's own slots are unaffected
    const mc::HudRect inv = off.invSlot(12);
    EXPECT_EQ(off.hitTest(inv.x + 1.f, inv.y + 1.f).kind, mc::SlotRef::Kind::Inventory);
    EXPECT_EQ(off.hitTest(inv.x + 1.f, inv.y + 1.f).index, 12);
}
