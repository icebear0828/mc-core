#pragma once

// The player's 36-slot inventory with Minecraft's click rules (survival inventory + the creative item palette). Slots 0..8 are
// the hotbar, 9..35 the main inventory. Pure logic, no game access.

#include "mc/types.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace mc {

enum class Button { Left, Right };

struct ItemStack {
    ItemId item{ItemId::None};
    uint32_t count{0};
    [[nodiscard]] bool empty() const { return item == ItemId::None || count == 0; }
};

inline uint32_t maxStackSize(ItemId item) {
    switch (item) {
        case ItemId::None: return 0;
        case ItemId::DiamondSword:
        case ItemId::DiamondPickaxe:
        case ItemId::Bow:
        case ItemId::Trident:
        case ItemId::FlintAndSteel:
        case ItemId::TotemOfUndying:
        case ItemId::Elytra: return 1;
        case ItemId::EnderPearl: return 16;
        default: return 64;
    }
}

class Inventory {
public:
    static constexpr int kHotbar = 9;
    static constexpr int kSlots = 36;

    // The hotbar the game starts with (same as the old MeleeController / Session::loadDefaultHotbar).
    static Inventory withDefaultHotbar() {
        Inventory inv;
        const ItemStack start[kHotbar] = {{ItemId::DiamondSword, 1}, {ItemId::DiamondPickaxe, 1}, {ItemId::BlockDirt, 64},
                                          {ItemId::BlockStone, 64},  {ItemId::BlockTnt, 16},       {ItemId::GoldenApple, 8},
                                          {ItemId::Bow, 1},          {ItemId::Elytra, 1},          {ItemId::TotemOfUndying, 1}};
        for (int i = 0; i < kHotbar; ++i) inv.slots_[static_cast<size_t>(i)] = start[i];
        return inv;
    }

    [[nodiscard]] const ItemStack& slot(int i) const { return slots_[static_cast<size_t>(std::clamp(i, 0, kSlots - 1))]; }
    void setSlot(int i, ItemStack s) {
        if (i < 0 || i >= kSlots) return;
        if (s.empty()) s = {};
        slots_[static_cast<size_t>(i)] = s;
    }
    [[nodiscard]] const ItemStack& cursor() const { return cursor_; }

    // Adds a stack: existing partial stacks first, then empty slots, hotbar before the main inventory. Returns what did not fit.
    uint32_t add(ItemStack s) {
        if (s.empty()) return 0;
        const uint32_t cap = maxStackSize(s.item);
        for (ItemStack& t : slots_) {
            if (t.item != s.item || t.empty()) continue;
            const uint32_t room = cap > t.count ? cap - t.count : 0;
            const uint32_t move = std::min(room, s.count);
            t.count += move;
            s.count -= move;
            if (s.count == 0) return 0;
        }
        for (ItemStack& t : slots_) {
            if (!t.empty()) continue;
            const uint32_t move = std::min(cap, s.count);
            t = {s.item, move};
            s.count -= move;
            if (s.count == 0) return 0;
        }
        return s.count;
    }

    bool consume(int i) {
        if (i < 0 || i >= kSlots || slots_[static_cast<size_t>(i)].empty()) return false;
        ItemStack& t = slots_[static_cast<size_t>(i)];
        if (--t.count == 0) t = {};
        return true;
    }
    [[nodiscard]] bool has(ItemId item) const {
        return std::any_of(slots_.begin(), slots_.end(), [&](const ItemStack& t) { return !t.empty() && t.item == item; });
    }
    bool consumeFirst(ItemId item) {
        for (int i = 0; i < kSlots; ++i) {
            if (!slots_[static_cast<size_t>(i)].empty() && slots_[static_cast<size_t>(i)].item == item) return consume(i);
        }
        return false;
    }

    // A click on slot `i`: Minecraft's pick up / place / merge / swap / split / shift-move rules.
    void click(int i, Button button, bool shift = false) {
        if (i < 0 || i >= kSlots) return;
        ItemStack& t = slots_[static_cast<size_t>(i)];
        if (shift) {
            shiftMove(i);
            return;
        }
        if (button == Button::Left) {
            if (cursor_.empty()) {
                cursor_ = t;
                t = {};
            } else if (t.empty()) {
                t = cursor_;
                cursor_ = {};
            } else if (t.item == cursor_.item) {
                const uint32_t cap = maxStackSize(t.item);
                const uint32_t move = std::min(cap > t.count ? cap - t.count : 0, cursor_.count);
                t.count += move;
                cursor_.count -= move;
                if (cursor_.count == 0) cursor_ = {};
            } else {
                std::swap(t, cursor_);
            }
            return;
        }
        if (cursor_.empty()) {
            if (t.empty()) return;
            const uint32_t take = (t.count + 1) / 2;
            cursor_ = {t.item, take};
            t.count -= take;
            if (t.count == 0) t = {};
        } else if (t.empty() || (t.item == cursor_.item && t.count < maxStackSize(t.item))) {
            if (t.empty()) t = {cursor_.item, 0};
            ++t.count;
            if (--cursor_.count == 0) cursor_ = {};
        } else {
            std::swap(t, cursor_); // different item, or a full / unstackable stack
        }
    }
    void clickOutside() { cursor_ = {}; }

    // The creative item palette: left takes a full stack (replacing a different item on the cursor), right takes one,
    // shift puts a full stack straight into the inventory.
    void clickPalette(ItemId item, Button button, bool shift = false) {
        const uint32_t cap = maxStackSize(item);
        if (cap == 0) return;
        if (shift) {
            add({item, cap});
            return;
        }
        if (button == Button::Left) {
            cursor_ = {item, cap};
        } else if (cursor_.item == item && !cursor_.empty()) {
            if (cursor_.count < cap) ++cursor_.count;
        } else {
            cursor_ = {item, 1};
        }
    }

    // Number key over a slot: swap it with that hotbar slot.
    void swapWithHotbar(int i, int hotbar) {
        if (i < 0 || i >= kSlots || hotbar < 0 || hotbar >= kHotbar || i == hotbar) return;
        std::swap(slots_[static_cast<size_t>(i)], slots_[static_cast<size_t>(hotbar)]);
    }

private:
    void shiftMove(int i) {
        ItemStack& src = slots_[static_cast<size_t>(i)];
        if (src.empty()) return;
        const int first = i < kHotbar ? kHotbar : 0;
        const int last = i < kHotbar ? kSlots : kHotbar;
        const uint32_t cap = maxStackSize(src.item);
        for (int pass = 0; pass < 2 && !src.empty(); ++pass) {
            for (int j = first; j < last && !src.empty(); ++j) {
                ItemStack& d = slots_[static_cast<size_t>(j)];
                if (pass == 0 ? (d.empty() || d.item != src.item) : !d.empty()) continue;
                const uint32_t room = pass == 0 ? (cap > d.count ? cap - d.count : 0) : cap;
                const uint32_t move = std::min(room, src.count);
                if (move == 0) continue;
                if (d.empty()) d = {src.item, 0};
                d.count += move;
                src.count -= move;
            }
        }
        if (src.count == 0) src = {};
    }

    std::array<ItemStack, kSlots> slots_{};
    ItemStack cursor_{};
};

} // namespace mc
