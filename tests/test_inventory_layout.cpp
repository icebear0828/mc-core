#include <gtest/gtest.h>

#include "mc/inventory_layout.hpp"

using namespace mc;

TEST(InventoryLayout, PanelIsCentredAndAnIntegerMultipleOfTheGuiScale) {
    InventoryLayout l(1920.f, 1080.f);
    EXPECT_EQ(l.scale(), 3);
    const HudRect p = l.panel();
    EXPECT_FLOAT_EQ(p.w, 176.f * 3);
    EXPECT_FLOAT_EQ(p.h, 166.f * 3);
    EXPECT_NEAR(p.x + p.w / 2, 960.f, 1.5f);
    EXPECT_NEAR(p.y + p.h / 2, 540.f, 1.5f);
}

TEST(InventoryLayout, ScaleFollowsTheHudScaleAndNeverOverflowsTheScreen) {
    InventoryLayout small(1280.f, 720.f);
    EXPECT_EQ(small.scale(), 2);
    InventoryLayout huge(7680.f, 3000.f);
    EXPECT_LE(huge.panel().h, 3000.f * 0.95f);
    InventoryLayout tiny(300.f, 150.f);
    EXPECT_EQ(tiny.scale(), 1);
}

TEST(InventoryLayout, SlotsFollowTheVanillaInventoryContainer) {
    InventoryLayout l(1920.f, 1080.f);
    const HudRect p = l.panel();
    const float s = static_cast<float>(l.scale());
    const HudRect first_main = l.invSlot(9);
    EXPECT_FLOAT_EQ(first_main.x, p.x + 8 * s);
    EXPECT_FLOAT_EQ(first_main.y, p.y + 84 * s);
    EXPECT_FLOAT_EQ(first_main.w, 16.f * s);
    const HudRect hot = l.invSlot(0);
    EXPECT_FLOAT_EQ(hot.y, p.y + 142 * s);
    EXPECT_FLOAT_EQ(l.invSlot(4).x, p.x + (8 + 18 * 4) * s);
    EXPECT_FLOAT_EQ(l.invSlot(10).y, first_main.y); // same row
    EXPECT_FLOAT_EQ(l.invSlot(18).y, first_main.y + 18 * s);
}

TEST(InventoryLayout, HitTestFindsSlotsAndMissesTheGaps) {
    InventoryLayout l(1920.f, 1080.f);
    const HudRect s = l.invSlot(13);
    SlotRef r = l.hitTest(s.x + 5, s.y + 5);
    EXPECT_EQ(r.kind, SlotRef::Kind::Inventory);
    EXPECT_EQ(r.index, 13);
    r = l.hitTest(l.invSlot(0).x + 1, l.invSlot(0).y + 1);
    EXPECT_EQ(r.kind, SlotRef::Kind::Inventory);
    EXPECT_EQ(r.index, 0);
    EXPECT_EQ(l.hitTest(1.f, 1.f).kind, SlotRef::Kind::None);
    const HudRect p = l.panel();
    // Hit inside the panel gap between armor and preview returns Panel
    EXPECT_EQ(l.hitTest(p.x + 2.f, p.y + 2.f).kind, SlotRef::Kind::Panel);
    EXPECT_EQ(l.hitTest(p.x + p.w + 10.f, p.y).kind, SlotRef::Kind::None);
}

TEST(InventoryLayout, ArmorAndOffhandAreInsidePanelAndFollowVanillaLayout) {
    InventoryLayout l(1920.f, 1080.f, false);
    const HudRect p = l.panel();
    const float s = static_cast<float>(l.scale());

    // 4 Armor slots on the left (x=8, y=8, 26, 44, 62)
    for (int i = 0; i < 4; ++i) {
        const HudRect arm = l.armorSlot(i);
        EXPECT_FLOAT_EQ(arm.x, p.x + 8.f * s);
        EXPECT_FLOAT_EQ(arm.y, p.y + (8.f + 18.f * i) * s);
        EXPECT_FLOAT_EQ(arm.w, 18.f * s);
        EXPECT_FLOAT_EQ(arm.h, 18.f * s);

        // Entire slot strictly within panel boundary
        EXPECT_GE(arm.x, p.x);
        EXPECT_LE(arm.x + arm.w, p.x + p.w);
        EXPECT_GE(arm.y, p.y);
        EXPECT_LE(arm.y + arm.h, p.y + p.h);

        SlotRef r = l.hitTest(arm.x + 5.f, arm.y + 5.f);
        EXPECT_EQ(r.kind, SlotRef::Kind::Armor);
        EXPECT_EQ(r.index, i);
    }

    // Offhand slot at the bottom-right of preview area (x=77, y=62)
    const HudRect off = l.offhandSlot();
    EXPECT_FLOAT_EQ(off.x, p.x + 77.f * s);
    EXPECT_FLOAT_EQ(off.y, p.y + 62.f * s);
    EXPECT_FLOAT_EQ(off.w, 18.f * s);
    EXPECT_FLOAT_EQ(off.h, 18.f * s);
    EXPECT_GE(off.x, p.x);
    EXPECT_LE(off.x + off.w, p.x + p.w);

    SlotRef ro = l.hitTest(off.x + 5.f, off.y + 5.f);
    EXPECT_EQ(ro.kind, SlotRef::Kind::Offhand);
    EXPECT_EQ(ro.index, 0);

    // Player preview window area (x=26, y=8, w=51, h=72)
    const HudRect prev = l.playerPreview();
    EXPECT_FLOAT_EQ(prev.x, p.x + 26.f * s);
    EXPECT_FLOAT_EQ(prev.y, p.y + 8.f * s);
    EXPECT_FLOAT_EQ(prev.w, 51.f * s);
    EXPECT_FLOAT_EQ(prev.h, 72.f * s);

    // Crafting 2x2 grid and result
    for (int i = 0; i < 4; ++i) {
        const HudRect cs = l.craftSlot(i);
        const int col = i % 2;
        const int row = i / 2;
        EXPECT_FLOAT_EQ(cs.x, p.x + (98.f + 18.f * col) * s);
        EXPECT_FLOAT_EQ(cs.y, p.y + (18.f + 18.f * row) * s);
    }
    const HudRect cr = l.craftResultSlot();
    EXPECT_FLOAT_EQ(cr.x, p.x + 154.f * s);
    EXPECT_FLOAT_EQ(cr.y, p.y + 28.f * s);
}

TEST(InventoryLayout, CreativeModeHitTestExcludesArmorAndOffhand) {
    InventoryLayout creative(1920.f, 1080.f, true);
    // In creative mode, clicking palette slot 0 must return Palette, never Armor
    const HudRect pal0 = creative.paletteSlot(0);
    const SlotRef r = creative.hitTest(pal0.x + 5.f, pal0.y + 5.f);
    EXPECT_EQ(r.kind, SlotRef::Kind::Palette);
    EXPECT_EQ(r.index, 0);

    // Clicking offhand coordinate in creative mode must return Panel or Palette, never Offhand
    const HudRect off = creative.offhandSlot();
    const SlotRef roff = creative.hitTest(off.x + 5.f, off.y + 5.f);
    EXPECT_NE(roff.kind, SlotRef::Kind::Offhand);
    EXPECT_NE(roff.kind, SlotRef::Kind::Armor);
}

TEST(InventoryLayout, PaletteListsEveryItemOnce) {
    const auto& items = paletteItems();
    EXPECT_EQ(items.size(), 25u); // 21 + 4 diamond armor items
    EXPECT_EQ(items.front(), ItemId::DiamondSword);
    for (size_t i = 0; i < items.size(); ++i) {
        EXPECT_NE(items[i], ItemId::None);
        for (size_t j = i + 1; j < items.size(); ++j) EXPECT_NE(items[i], items[j]);
    }
    EXPECT_LE(items.size(), static_cast<size_t>(InventoryLayout::kPaletteSlots));
}

