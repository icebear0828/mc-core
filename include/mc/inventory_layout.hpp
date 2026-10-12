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
        ItemId::CookedBeef,   ItemId::TotemOfUndying, ItemId::Shield,      ItemId::Elytra,      ItemId::FireworkRocket,
        ItemId::BlockDirt,    ItemId::BlockStone,     ItemId::BlockTnt,    ItemId::ZombieSpawnEgg,
        ItemId::DiamondHelmet, ItemId::DiamondChestplate, ItemId::DiamondLeggings, ItemId::DiamondBoots};
    return items;
}

struct SlotRef {
    enum class Kind { None, Panel, Palette, Inventory, Offhand, Armor, Crafting, CraftResult };
    Kind kind{Kind::None};
    int index{-1}; // palette slot 0..26, inventory slot 0..35, offhand 0, armor 0..3, crafting 0..3, craft result 0
};

class InventoryLayout {
public:
    static constexpr int kPanelW = 176;
    static constexpr int kPanelH = 166;
    static constexpr int kPaletteSlots = 27;
    static constexpr int kTopHeight = 71;

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
        if (i < 9) return slotAt(8 + 18 * i, 142);
        const int m = i - 9;
        return slotAt(8 + 18 * (m % 9), 84 + 18 * (m / 9));
    }
    // Minecraft Java Edition vanilla inventory.png layout:
    // 4 Armor slots on the left (x = 8, y = 8, 26, 44, 62)
    [[nodiscard]] HudRect armorSlot(int i) const { return slotAt(8, 8 + 18 * std::clamp(i, 0, 3), 18.f); }
    [[nodiscard]] HudRect armorSlot(ArmorSlot slot) const { return armorSlot(static_cast<int>(slot)); }
    // Offhand slot in inventory: 18x18 GUI px slot positioned to the right of boots / bottom-right of preview at X = 77, Y = 62
    [[nodiscard]] HudRect offhandSlot() const { return slotAt(77, 62, 18.f); }
    // Player 3D entity preview window area (x = 26..76, y = 8..79)
    [[nodiscard]] HudRect playerPreview() const { return slotAt(26, 8, 51.f, 72.f); }
    // 2x2 Crafting slots (x = 98 / 116, y = 18 / 36)
    [[nodiscard]] HudRect craftSlot(int i) const {
        const int col = std::clamp(i, 0, 3) % 2;
        const int row = std::clamp(i, 0, 3) / 2;
        return slotAt(98 + 18 * col, 18 + 18 * row, 18.f);
    }
    // Crafting result output slot (x = 154, y = 28)
    [[nodiscard]] HudRect craftResultSlot() const { return slotAt(154, 28, 18.f); }
    // Where the section titles ("Crafting", "Inventory") start, in screen pixels.
    [[nodiscard]] HudRect titleCrafting() const { return slotAt(97, 6, 0.f); }
    [[nodiscard]] HudRect titleItems() const { return slotAt(8, 6, 0.f); }
    [[nodiscard]] HudRect titleInventory() const { return slotAt(8, 72, 0.f); }

    [[nodiscard]] SlotRef hitTest(float x, float y) const {
        for (int i = 0; i < 4; ++i) {
            if (inside(armorSlot(i), x, y)) return {SlotRef::Kind::Armor, i};
        }
        if (inside(offhandSlot(), x, y)) return {SlotRef::Kind::Offhand, 0};
        for (int i = 0; i < 4; ++i) {
            if (inside(craftSlot(i), x, y)) return {SlotRef::Kind::Crafting, i};
        }
        if (inside(craftResultSlot(), x, y)) return {SlotRef::Kind::CraftResult, 0};
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
    [[nodiscard]] HudRect slotAt(int gx, int gy, float w, float h) const {
        const float s = static_cast<float>(scale_);
        return {panel_.x + gx * s, panel_.y + gy * s, w * s, h * s};
    }
    static bool inside(const HudRect& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

    int scale_{1};
    HudRect panel_{};
    bool palette_{true};
};

} // namespace mc
