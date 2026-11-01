#pragma once

// The crafting table's screen state: a 3x3 grid, the recipe currently matching it and the click
// rules that move stacks between the grid, the cursor and the player's inventory. Pure logic.

#include "mc/inventory.hpp"
#include "mc/recipe.hpp"
#include "mc/types.hpp"

#include <span>

namespace mc {

class CraftingSession {
public:
    explicit CraftingSession(std::span<const Recipe> recipes = defaultRecipes()) : book_(recipes) {}

    [[nodiscard]] const CraftingGrid& grid() const { return grid_; }
    // The recipe the grid currently matches, or nullptr.
    [[nodiscard]] const Recipe* result() const { return book_.find(grid_); }
    [[nodiscard]] bool hasResult() const { return result() != nullptr; }
    [[nodiscard]] ItemStack resultStack() const {
        const Recipe* r = result();
        return r != nullptr ? r->result : ItemStack{};
    }

    // A click on input cell (x, y). Left/right follow the inventory's pick up / place / merge /
    // swap / split rules; shift moves the whole stack to `inv`.
    void clickGrid(int x, int y, Button button, bool shift, Inventory& inv);

    // A click on the result slot: one craft, or with shift as many as fit. Returns how many were
    // crafted. Crafting is atomic and consumes one item per occupied cell.
    int clickResult(Button button, bool shift, Inventory& inv);

    // Put every grid stack back into `inv`. What does not fit stays in the grid (there is no drop
    // system in core), so nothing is lost.
    void returnGridTo(Inventory& inv);

    void clear();

private:
    CraftingGrid grid_{};
    RecipeBook book_;
};

} // namespace mc
