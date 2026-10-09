#include <gtest/gtest.h>

#include "mc/hud_layout.hpp"

#include <cmath>

using mc::HudLayout;
using mc::HudRect;

namespace {
void expectRect(const HudRect& r, float x, float y, float w, float h) {
    EXPECT_FLOAT_EQ(r.x, x);
    EXPECT_FLOAT_EQ(r.y, y);
    EXPECT_FLOAT_EQ(r.w, w);
    EXPECT_FLOAT_EQ(r.h, h);
}
} // namespace

TEST(HudLayoutTest, GuiScaleIsAnIntegerThatGrowsWithResolution) {
    EXPECT_EQ(HudLayout::guiScaleFor(480.f), 1);
    EXPECT_EQ(HudLayout::guiScaleFor(720.f), 2);
    EXPECT_EQ(HudLayout::guiScaleFor(1080.f), 3);
    EXPECT_EQ(HudLayout::guiScaleFor(1440.f), 4);
    EXPECT_EQ(HudLayout::guiScaleFor(2160.f), 6);
    EXPECT_EQ(HudLayout::guiScaleFor(100.f), 1); // never zero
}

TEST(HudLayoutTest, HotbarIsCentredOnTheBottomEdgeAt1080p) {
    const HudLayout l(1920.f, 1080.f);
    ASSERT_EQ(l.scale(), 3);
    expectRect(l.hotbar(), 687.f, 1014.f, 546.f, 66.f);  // (1920 - 182*3)/2, 1080 - 22*3
}

TEST(HudLayoutTest, SelectionAndItemsFollowTheTwentyPixelSlotPitch) {
    const HudLayout l(1920.f, 1080.f);
    expectRect(l.selection(0), 684.f, 1011.f, 72.f, 69.f);   // one GUI px outside the slot
    expectRect(l.selection(4), 684.f + 240.f, 1011.f, 72.f, 69.f);
    expectRect(l.item(0), 687.f + 9.f, 1014.f + 9.f, 48.f, 48.f);
    expectRect(l.item(2), 687.f + 129.f, 1023.f, 48.f, 48.f);  // (3 + 2*20) * 3
}

TEST(HudLayoutTest, HeartsOverlapByOneGuiPixelAndSitAboveTheHotbar) {
    const HudLayout l(1920.f, 1080.f);
    expectRect(l.heart(0), 687.f, 963.f, 27.f, 27.f);  // 1080 - 39*3
    EXPECT_FLOAT_EQ(l.heart(1).x - l.heart(0).x, 24.f); // 8 GUI px pitch ...
    EXPECT_LT(l.heart(1).x, l.heart(0).x + l.heart(0).w); // ... but 9 GUI px wide: they overlap
    EXPECT_FLOAT_EQ(l.heart(0).x + l.heart(0).w - l.heart(1).x, 3.f);
    EXPECT_FLOAT_EQ(l.heart(9).x, 687.f + 216.f);
}

TEST(HudLayoutTest, FoodRunsRightToLeftAndEndsFlushWithTheHotbar) {
    const HudLayout l(1920.f, 1080.f);
    const HudRect bar = l.hotbar();
    EXPECT_FLOAT_EQ(l.food(0).x + l.food(0).w, bar.x + bar.w); // rightmost icon touches the bar's right edge
    EXPECT_FLOAT_EQ(l.food(0).x - l.food(1).x, 24.f);
    EXPECT_FLOAT_EQ(l.food(0).y, l.heart(0).y);               // same row as the hearts
}

TEST(HudLayoutTest, CrosshairIsCentredOnAnOddSizedSprite) {
    const HudLayout l(1920.f, 1080.f);
    expectRect(l.crosshair(), 937.f, 517.f, 45.f, 45.f);  // floor((1920-45)/2), floor((1080-45)/2)
}

TEST(HudLayoutTest, EveryCoordinateIsAnExactIntegerSoPixelsStayCrisp) {
    for (float h : {720.f, 1080.f, 1440.f, 2160.f}) {
        const float w = h * 16.f / 9.f;
        const HudLayout l(w, h);
        auto integral = [&](const HudRect& r) {
            for (float v : {r.x, r.y, r.w, r.h}) EXPECT_FLOAT_EQ(v, std::round(v)) << "at " << h << "p";
            EXPECT_EQ(static_cast<int>(r.w) % l.scale(), 0);
            EXPECT_EQ(static_cast<int>(r.h) % l.scale(), 0);
        };
        integral(l.hotbar());
        integral(l.crosshair());
        for (int i = 0; i < 9; ++i) { integral(l.selection(i)); integral(l.item(i)); }
        for (int i = 0; i < 10; ++i) { integral(l.heart(i)); integral(l.food(i)); }
    }
}

TEST(HudLayoutTest, TextIsEightGuiPixelsTallAndTheItemNameSitsAboveTheStatusRow) {
    const HudLayout l(1920.f, 1080.f);
    EXPECT_FLOAT_EQ(l.textSize(), 24.f);
    EXPECT_FLOAT_EQ(l.itemNameBaselineY(), 1080.f - 59.f * 3.f);
    EXPECT_LT(l.itemNameBaselineY(), l.heart(0).y);
}

TEST(HudLayoutTest, ExperienceBarSitsAboveTheHotbarAtVanillaHeight) {
    mc::HudLayout l(1920.f, 1080.f);
    const mc::HudRect bar = l.xpBar();
    EXPECT_FLOAT_EQ(bar.x, l.hotbar().x);          // same left edge as the hotbar
    EXPECT_FLOAT_EQ(bar.w, 182.f * 3);
    EXPECT_FLOAT_EQ(bar.h, 5.f * 3);
    EXPECT_FLOAT_EQ(bar.y, 1080.f - 29.f * 3);     // height - 32 + 3 GUI px
    EXPECT_LT(bar.y + bar.h, l.hotbar().y);        // above the hotbar
    EXPECT_GT(bar.y, l.heart(0).y + l.heart(0).h - 1.f); // and below the hearts
    // the level number is centred over the bar, 35 GUI px above the bottom
    EXPECT_FLOAT_EQ(l.xpLevelTextTop(), 1080.f - 35.f * 3);
    EXPECT_FLOAT_EQ(l.xpLevelCentreX(), 960.f);
}
