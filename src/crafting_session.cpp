#include "mc/crafting_session.hpp"

#include <algorithm>

namespace mc {

void CraftingSession::clickGrid(int x, int y, Button button, bool shift, Inventory& inv) {
    if (x < 0 || x >= kCraftGridSize || y < 0 || y >= kCraftGridSize) return;
    ItemStack& slot = grid_.at(x, y);

    if (shift) {
        if (slot.empty()) return;
        const uint32_t left = inv.add(slot);
        slot.count = left;
        if (left == 0) slot = {};
        return;
    }

    ItemStack cursor = inv.takeCursor();
    if (button == Button::Left) {
        if (cursor.empty()) {
            cursor = slot;
            slot = {};
        } else if (slot.empty()) {
            slot = cursor;
            cursor = {};
        } else if (slot.item == cursor.item) {
            const uint32_t cap = maxStackSize(slot.item);
            const uint32_t move = std::min(cap > slot.count ? cap - slot.count : 0, cursor.count);
            slot.count += move;
            cursor.count -= move;
            if (cursor.count == 0) cursor = {};
        } else {
            std::swap(slot, cursor);
        }
    } else if (cursor.empty()) {
        if (!slot.empty()) {
            const uint32_t take = (slot.count + 1) / 2;
            cursor = {slot.item, take};
            slot.count -= take;
            if (slot.count == 0) slot = {};
        }
    } else if (slot.empty() || (slot.item == cursor.item && slot.count < maxStackSize(slot.item))) {
        if (slot.empty()) slot = {cursor.item, 0};
        ++slot.count;
        if (--cursor.count == 0) cursor = {};
    } else {
        std::swap(slot, cursor);
    }
    inv.setCursor(cursor);
}

int CraftingSession::clickResult(Button, bool shift, Inventory& inv) {
    const Recipe* r = result();
    if (r == nullptr) return 0;

    int made = 0;
    do {
        const CraftResult out = book_.craft(*r, grid_, inv);
        if (!out.ok) break;
        ++made;
        if (!shift || grid_.empty()) break;
        r = result();
    } while (r != nullptr && made < kCraftCellCount * 8);
    return made;
}

void CraftingSession::returnGridTo(Inventory& inv) {
    for (ItemStack& cell : grid_.cells) {
        if (cell.empty()) continue;
        const uint32_t left = inv.add(cell);
        cell.count = left;
        if (left == 0) cell = {};
    }
}

void CraftingSession::clear() { grid_.clear(); }

} // namespace mc
