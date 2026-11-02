#pragma once

// Text save format for the player's inventory (36 slots, armor, offhand, cursor), the selected
// hotbar slot and the crafting grid. The game gives the DLL no exit hook, so the adapter writes
// this periodically from a worker thread and reads it once at startup. Pure logic: no file access.

#include "mc/crafting_session.hpp"
#include "mc/inventory.hpp"
#include "mc/recipe.hpp"
#include "mc/types.hpp"

#include <string>
#include <string_view>

namespace mc {

inline constexpr int kInventorySaveVersion = 1;

// One past the last real ItemId. Bump this when an item is appended and give it a slug below;
// tests iterate every id up to it.
inline constexpr ItemId kLastItemId = ItemId::Torch;

// A stable lowercase slug ("diamond_sword"); empty for ItemId::None and ids past kLastItemId.
[[nodiscard]] std::string_view itemSlug(ItemId item);
[[nodiscard]] ItemId itemFromSlug(std::string_view slug);

// The save text. Empty slots are omitted; the cursor and the crafting grid are included.
[[nodiscard]] std::string serializeInventory(const Inventory& inv, int selected_slot, const CraftingGrid& grid);

struct InventoryLoadResult {
    bool ok{false};
    int loaded{0};     // stacks applied
    int skipped{0};    // unknown item slugs and malformed lines
    std::string error; // why ok is false
};

// Parses `text` into `inv`, `selected_slot` and `grid`. A bad header or unsupported version returns
// ok=false and leaves all three untouched; unknown items and malformed lines are skipped.
[[nodiscard]] InventoryLoadResult deserializeInventory(std::string_view text, Inventory& inv, int& selected_slot,
                                                       CraftingGrid& grid);

} // namespace mc
