#pragma once

// The 3x3 crafting recipe engine (issue #67). Pure logic: no host/game access, no exceptions.
//
// Matching follows Minecraft's rules:
//   - shaped recipes match at any offset in the 3x3 grid (the grid's bounding box is trimmed);
//   - a shaped recipe may also match its horizontal mirror when `mirror` is set;
//   - shapeless recipes match an order-independent multiset of item types;
//   - stack counts inside the grid do not affect matching, and crafting consumes exactly one
//     item per occupied cell;
//   - crafting is atomic: nothing is consumed unless the result can be added to the inventory.

#include "mc/inventory.hpp"
#include "mc/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mc {

inline constexpr int kCraftGridSize = 3;
inline constexpr int kCraftCellCount = kCraftGridSize * kCraftGridSize; // 9

// The 3x3 input grid, row-major (index = y * 3 + x). Empty cells have ItemId::None.
struct CraftingGrid {
    std::array<ItemStack, kCraftCellCount> cells{};

    [[nodiscard]] const ItemStack& at(int x, int y) const;
    [[nodiscard]] ItemStack& at(int x, int y);
    [[nodiscard]] bool empty() const;
    void clear();
    [[nodiscard]] int occupied() const; // number of non-empty cells
};

enum class RecipeKind : uint8_t { Shaped, Shapeless };

enum class RecipeCategory : uint8_t { WeaponUpgrade, Tactical, Material, Repair, Other };

struct RecipeIngredient {
    ItemId item{ItemId::None};
    uint8_t count{1}; // shapeless: number of slots the ingredient occupies
};

struct Recipe {
    RecipeKind kind{RecipeKind::Shaped};
    RecipeCategory category{RecipeCategory::Other};

    // Shaped: the pattern trimmed to width x height (1..3), row-major, ItemId::None = empty cell.
    std::array<ItemId, kCraftCellCount> pattern{};
    int width{0};
    int height{0};
    bool mirror{true}; // also match the horizontally flipped pattern

    // Shapeless: multiset of ingredient slots.
    std::array<RecipeIngredient, kCraftCellCount> ingredients{};
    int ingredient_count{0};

    ItemStack result{};
};

struct CraftResult {
    bool ok{false};
    ItemStack output{};
};

// The built-in recipe set (sword ladder, shield, anvil, TNT, flint and steel, golden apple,
// plus planks / sticks / crafting table). Defined in src/recipe.cpp.
[[nodiscard]] std::span<const Recipe> defaultRecipes() noexcept;

class RecipeBook {
public:
    explicit RecipeBook(std::span<const Recipe> recipes = defaultRecipes());

    [[nodiscard]] bool matches(const Recipe& r, const CraftingGrid& g) const;
    [[nodiscard]] const Recipe* find(const CraftingGrid& g) const;
    [[nodiscard]] std::vector<const Recipe*> craftable(const CraftingGrid& g) const;

    // Classic drag matching: consumes one item from each occupied cell and adds the result to
    // `inv`. Fails atomically (consuming nothing) when the grid does not match or the output
    // does not fit.
    [[nodiscard]] CraftResult craft(const Recipe& r, CraftingGrid& g, Inventory& inv) const;

    // Quick craft: takes the ingredients straight from `inv` (hotbar first) without a grid.
    // Fails atomically when an ingredient is missing or the output does not fit.
    [[nodiscard]] CraftResult quickCraft(const Recipe& r, Inventory& inv) const;

    [[nodiscard]] std::span<const Recipe> recipes() const { return recipes_; }

private:
    std::span<const Recipe> recipes_;
};

} // namespace mc
