#pragma once

// Geometry of the inventory screen: Minecraft's 3-row container (item palette on top, the player's 27 + 9 slots below)
// at the integer GUI scale. Pure logic: a panel rectangle, slot rectangles and a hit test for the mouse cursor.

#include "mc/hud_layout.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <vector>

namespace mc {

inline constexpr int kPaletteColumns = 9;
inline constexpr int kPaletteRows = 3;
inline constexpr int kPaletteSlots = kPaletteColumns * kPaletteRows;

// The items offered by the palette, in display order. More than one page: the screen shows
// kPaletteSlots at a time and the wheel changes the page.
inline const std::vector<ItemId>& paletteItems() {
    static const std::vector<ItemId> items = {
        // Tools and weapons.
        ItemId::DiamondSword, ItemId::WoodenSword, ItemId::StoneSword, ItemId::IronSword, ItemId::NetheriteSword,
        ItemId::DiamondPickaxe, ItemId::WoodenPickaxe, ItemId::StonePickaxe, ItemId::IronPickaxe, ItemId::GoldenPickaxe,
        ItemId::NetheritePickaxe, ItemId::Bow, ItemId::Arrow, ItemId::Trident, ItemId::Shield, ItemId::Elytra,
        // Food and utility.
        ItemId::GoldenApple, ItemId::EnchantedGoldenApple, ItemId::Bread, ItemId::CookedBeef, ItemId::TotemOfUndying,
        ItemId::EnderPearl, ItemId::FireworkRocket, ItemId::FlintAndSteel,
        // Armor.
        ItemId::DiamondHelmet, ItemId::DiamondChestplate, ItemId::DiamondLeggings, ItemId::DiamondBoots,
        // Blocks.
        ItemId::BlockDirt, ItemId::BlockStone, ItemId::BlockCobblestone, ItemId::BlockWood, ItemId::BlockObsidian,
        ItemId::BlockTnt, ItemId::WoodPlanks, ItemId::CraftingTable, ItemId::Anvil, ItemId::Sand,
        // Materials.
        ItemId::Coal, ItemId::RawCopper, ItemId::RawIron, ItemId::RawGold, ItemId::Redstone, ItemId::LapisLazuli,
        ItemId::AmethystShard, ItemId::Diamond, ItemId::Emerald, ItemId::NetheriteScrap, ItemId::IronIngot,
        ItemId::GoldIngot, ItemId::NetheriteIngot, ItemId::Stick, ItemId::String, ItemId::Gunpowder, ItemId::Flint,
        ItemId::Apple,
        // Spawn eggs.
        ItemId::ZombieSpawnEgg, ItemId::CreeperSpawnEgg, ItemId::SkeletonSpawnEgg};
    return items;
}

inline int palettePageCount() {
    const int n = static_cast<int>(paletteItems().size());
    return std::max(1, (n + kPaletteSlots - 1) / kPaletteSlots);
}

// The item on a page's slot, or None when the page/slot has no item.
inline ItemId paletteItemAt(int page, int slot) {
    if (page < 0 || slot < 0 || slot >= kPaletteSlots) return ItemId::None;
    const size_t index = static_cast<size_t>(page * kPaletteSlots + slot);
    const auto& items = paletteItems();
    return index < items.size() ? items[index] : ItemId::None;
}

// The page that holds `item`, or 0 when it is not in the palette.
inline int palettePageFor(ItemId item) {
    const auto& items = paletteItems();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i] == item) return static_cast<int>(i) / kPaletteSlots;
    }
    return 0;
}

struct SlotRef {
    enum class Kind { None, Panel, Palette, PalettePrev, PaletteNext, Inventory, Offhand, Armor, Crafting, CraftResult };
    Kind kind{Kind::None};
    int index{-1}; // palette slot 0..26, inventory slot 0..35, offhand 0, armor 0..3, crafting 0..3, craft result 0
};

class InventoryLayout {
public:
    static constexpr int kPanelW = 176;
    static constexpr int kPanelH = 166;
    static constexpr int kPaletteSlots = mc::kPaletteSlots;
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
    // Page arrows in the free strip above the palette slots (y = 0..17), next to the "Items" title.
    [[nodiscard]] HudRect palettePrevButton() const { return slotAt(122, 3, 12.f, 12.f); }
    [[nodiscard]] HudRect paletteNextButton() const { return slotAt(158, 3, 12.f, 12.f); }
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
        if (!palette_) {
            for (int i = 0; i < 4; ++i) {
                if (inside(armorSlot(i), x, y)) return {SlotRef::Kind::Armor, i};
            }
            if (inside(offhandSlot(), x, y)) return {SlotRef::Kind::Offhand, 0};
            for (int i = 0; i < 4; ++i) {
                if (inside(craftSlot(i), x, y)) return {SlotRef::Kind::Crafting, i};
            }
            if (inside(craftResultSlot(), x, y)) return {SlotRef::Kind::CraftResult, 0};
        }
        if (x < panel_.x || y < panel_.y || x >= panel_.x + panel_.w || y >= panel_.y + panel_.h) return {};
        if (palette_) {
            if (inside(palettePrevButton(), x, y)) return {SlotRef::Kind::PalettePrev, 0};
            if (inside(paletteNextButton(), x, y)) return {SlotRef::Kind::PaletteNext, 0};
        }
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
