#pragma once

// Geometry of the inventory screen: Minecraft's 3-row container (item palette on top, the player's 27 + 9 slots below)
// at the integer GUI scale. Pure logic: a panel rectangle, slot rectangles and a hit test for the mouse cursor.

#include "mc/hud_layout.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <vector>

namespace mc {

// The items offered by the palette, in display order.
inline const std::vector<ItemId>& paletteItems() {
    static const std::vector<ItemId> items = {
        ItemId::DiamondSword, ItemId::DiamondPickaxe, ItemId::Bow,         ItemId::Arrow,        ItemId::Trident,
        ItemId::FlintAndSteel, ItemId::EnderPearl,    ItemId::GoldenApple, ItemId::EnchantedGoldenApple, ItemId::Bread,
        ItemId::CookedBeef,   ItemId::TotemOfUndying, ItemId::Elytra,      ItemId::FireworkRocket, ItemId::BlockDirt,
        ItemId::BlockStone,   ItemId::BlockTnt};
    return items;
}

struct SlotRef {
    enum class Kind { None, Panel, Palette, Inventory };
    Kind kind{Kind::None};
    int index{-1}; // palette slot 0..26, or inventory slot 0..35
};

class InventoryLayout {
public:
    static constexpr int kPanelW = 176;
    static constexpr int kPanelH = 167;
    static constexpr int kPaletteSlots = 27;
    static constexpr int kTopHeight = 71; // title bar + 3 rows of palette

    // palette = false (survival): the item palette is not offered, its slots only belong to the panel.
    InventoryLayout(float screen_w, float screen_h, bool palette = true) : palette_(palette) {
        scale_ = std::max(1, std::min(HudLayout::guiScaleFor(screen_h), static_cast<int>(screen_h * 0.95f) / kPanelH));
        const float s = static_cast<float>(scale_);
        panel_ = {static_cast<float>(static_cast<int>((screen_w - kPanelW * s) / 2.f)),
                  static_cast<float>(static_cast<int>((screen_h - kPanelH * s) / 2.f)), kPanelW * s, kPanelH * s};
    }

    [[nodiscard]] int scale() const { return scale_; }
    [[nodiscard]] HudRect panel() const { return panel_; }
    // 16x16 GUI px item area of a slot.
    [[nodiscard]] HudRect paletteSlot(int i) const { return slotAt(8 + 18 * (i % 9), 18 + 18 * (i / 9)); }
    [[nodiscard]] HudRect invSlot(int i) const {
        if (i < 9) return slotAt(8 + 18 * i, 143);
        const int m = i - 9;
        return slotAt(8 + 18 * (m % 9), 85 + 18 * (m / 9));
    }
    // Where the section titles ("Items", "Inventory") start, in screen pixels.
    [[nodiscard]] HudRect titleItems() const { return slotAt(8, 6, 0.f); }
    [[nodiscard]] HudRect titleInventory() const { return slotAt(8, 73, 0.f); }

    [[nodiscard]] SlotRef hitTest(float x, float y) const {
        if (x < panel_.x || y < panel_.y || x >= panel_.x + panel_.w || y >= panel_.y + panel_.h) return {};
        for (int i = 0; palette_ && i < kPaletteSlots; ++i) {
            if (inside(paletteSlot(i), x, y)) return {SlotRef::Kind::Palette, i};
        }
        for (int i = 0; i < 36; ++i) {
            if (inside(invSlot(i), x, y)) return {SlotRef::Kind::Inventory, i};
        }
        return {SlotRef::Kind::Panel, -1};
    }

private:
    [[nodiscard]] HudRect slotAt(int gx, int gy, float size = 16.f) const {
        const float s = static_cast<float>(scale_);
        return {panel_.x + gx * s, panel_.y + gy * s, size * s, size * s};
    }
    static bool inside(const HudRect& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

    int scale_{1};
    HudRect panel_{};
    bool palette_{true};
};

} // namespace mc
