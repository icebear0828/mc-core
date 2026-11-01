#pragma once

// Geometry of Minecraft's crafting table screen: the 3x3 input grid, the result slot and the
// player's 27 + 9 slots below. Pure logic: a panel rectangle, slot rectangles and a hit test.

#include "mc/hud_layout.hpp"
#include "mc/inventory.hpp"
#include "mc/inventory_layout.hpp"
#include "mc/types.hpp"

#include <algorithm>

namespace mc {

class CraftingTableLayout {
public:
    static constexpr int kPanelW = 176;
    static constexpr int kPanelH = 166;
    static constexpr int kGridSize = 3;
    static constexpr int kGridSlots = kGridSize * kGridSize;

    CraftingTableLayout(float screen_w, float screen_h) {
        scale_ = std::max(1, std::min(HudLayout::guiScaleFor(screen_h), static_cast<int>(screen_h * 0.95f) / kPanelH));
        const float s = static_cast<float>(scale_);
        panel_ = {static_cast<float>(static_cast<int>((screen_w - kPanelW * s) / 2.f)),
                  static_cast<float>(static_cast<int>((screen_h - kPanelH * s) / 2.f)), kPanelW * s, kPanelH * s};
    }

    [[nodiscard]] int scale() const { return scale_; }
    [[nodiscard]] HudRect panel() const { return panel_; }

    // Input grid slot i = y * 3 + x (row-major).
    [[nodiscard]] HudRect gridSlot(int i) const {
        const int col = std::clamp(i, 0, kGridSlots - 1) % kGridSize;
        const int row = std::clamp(i, 0, kGridSlots - 1) / kGridSize;
        return slotAt(30 + 18 * col, 17 + 18 * row);
    }
    [[nodiscard]] HudRect resultSlot() const { return slotAt(124, 35); }

    // Player slots: 0..8 hotbar, 9..35 main inventory.
    [[nodiscard]] HudRect invSlot(int i) const {
        if (i < 9) return slotAt(8 + 18 * i, 142);
        const int m = i - 9;
        return slotAt(8 + 18 * (m % 9), 84 + 18 * (m / 9));
    }

    [[nodiscard]] HudRect titleCrafting() const { return slotAt(29, 6, 0.f); }
    [[nodiscard]] HudRect titleInventory() const { return slotAt(8, 72, 0.f); }

    [[nodiscard]] SlotRef hitTest(float x, float y) const {
        if (x < panel_.x || y < panel_.y || x >= panel_.x + panel_.w || y >= panel_.y + panel_.h) return {};
        for (int i = 0; i < kGridSlots; ++i) {
            if (inside(gridSlot(i), x, y)) return {SlotRef::Kind::Crafting, i};
        }
        if (inside(resultSlot(), x, y)) return {SlotRef::Kind::CraftResult, 0};
        for (int i = 0; i < Inventory::kSlots; ++i) {
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
};

} // namespace mc
