#include "mc/recipe.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace mc {

namespace {

ItemId itemInCell(const CraftingGrid& grid, int x, int y) {
    const ItemStack& stack = grid.at(x, y);
    return stack.empty() ? ItemId::None : stack.item;
}

bool matchesShaped(const Recipe& r, const CraftingGrid& grid) {
    if (r.width <= 0 || r.height <= 0 || r.width > kCraftGridSize || r.height > kCraftGridSize) return false;

    // Trim the grid to the bounding box of its non-empty cells. Matching the trimmed box is
    // equivalent to trying every offset, and makes items outside the pattern fail naturally.
    int min_x = kCraftGridSize;
    int min_y = kCraftGridSize;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < kCraftGridSize; ++y) {
        for (int x = 0; x < kCraftGridSize; ++x) {
            if (!grid.at(x, y).empty()) {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }
    }
    if (max_x < 0) return false; // an empty grid never matches
    if (max_x - min_x + 1 != r.width || max_y - min_y + 1 != r.height) return false;

    const bool mirrors[2] = {false, r.mirror};
    for (bool mirror : mirrors) {
        bool ok = true;
        for (int py = 0; py < r.height && ok; ++py) {
            for (int px = 0; px < r.width; ++px) {
                const int pattern_x = mirror ? (r.width - 1 - px) : px;
                const ItemId expected = r.pattern[static_cast<size_t>(py * r.width + pattern_x)];
                const ItemId actual = itemInCell(grid, min_x + px, min_y + py);
                if (expected != actual) {
                    ok = false;
                    break;
                }
            }
        }
        if (ok) return true;
    }
    return false;
}

bool matchesShapeless(const Recipe& r, const CraftingGrid& grid) {
    std::array<ItemId, kCraftCellCount> have{};
    int have_count = 0;
    for (const ItemStack& cell : grid.cells) {
        if (!cell.empty() && have_count < kCraftCellCount) have[static_cast<size_t>(have_count++)] = cell.item;
    }

    std::array<ItemId, kCraftCellCount> need{};
    int need_count = 0;
    for (int i = 0; i < r.ingredient_count && i < kCraftCellCount; ++i) {
        const RecipeIngredient& ingredient = r.ingredients[static_cast<size_t>(i)];
        const int repeats = std::max<int>(1, ingredient.count);
        for (int k = 0; k < repeats && need_count < kCraftCellCount; ++k) {
            need[static_cast<size_t>(need_count++)] = ingredient.item;
        }
    }

    if (have_count != need_count) return false;
    std::sort(have.begin(), have.begin() + have_count);
    std::sort(need.begin(), need.begin() + need_count);
    for (int i = 0; i < have_count; ++i) {
        if (have[static_cast<size_t>(i)] != need[static_cast<size_t>(i)]) return false;
    }
    return true;
}

// Expand a recipe's ingredient list into the item types one craft needs. Shaped recipes expand
// their pattern; shapeless recipes expand their ingredient counts.
int expandIngredients(const Recipe& r, std::array<ItemId, kCraftCellCount>& out) {
    int n = 0;
    if (r.kind == RecipeKind::Shaped) {
        const int cells = std::min(kCraftCellCount, r.width * r.height);
        for (int i = 0; i < cells; ++i) {
            const ItemId id = r.pattern[static_cast<size_t>(i)];
            if (id != ItemId::None && n < kCraftCellCount) out[static_cast<size_t>(n++)] = id;
        }
    } else {
        for (int i = 0; i < r.ingredient_count && i < kCraftCellCount; ++i) {
            const RecipeIngredient& ingredient = r.ingredients[static_cast<size_t>(i)];
            const int repeats = std::max<int>(1, ingredient.count);
            for (int k = 0; k < repeats && n < kCraftCellCount; ++k) out[static_cast<size_t>(n++)] = ingredient.item;
        }
    }
    return n;
}

Recipe shaped(RecipeCategory category, int width, int height, std::initializer_list<ItemId> pattern, ItemStack result,
              bool mirror = true) {
    Recipe r;
    r.kind = RecipeKind::Shaped;
    r.category = category;
    r.width = width;
    r.height = height;
    r.mirror = mirror;
    int i = 0;
    for (ItemId id : pattern) {
        if (i >= kCraftCellCount) break;
        r.pattern[static_cast<size_t>(i++)] = id;
    }
    r.result = result;
    return r;
}

Recipe shapeless(RecipeCategory category, std::initializer_list<RecipeIngredient> ingredients, ItemStack result) {
    Recipe r;
    r.kind = RecipeKind::Shapeless;
    r.category = category;
    int i = 0;
    for (const RecipeIngredient& ingredient : ingredients) {
        if (i >= kCraftCellCount) break;
        r.ingredients[static_cast<size_t>(i++)] = ingredient;
    }
    r.ingredient_count = i;
    r.result = result;
    return r;
}

constexpr ItemId kNone = ItemId::None;

// The built-in recipe set. Patterns are trimmed to their bounding box; ItemId::None is an empty
// cell. There is no furnace in this project yet, so the recipes use raw materials directly.
const std::array<Recipe, 13> kRecipes{{
    // Materials.
    shapeless(RecipeCategory::Material, {{ItemId::BlockWood, 1}}, {ItemId::WoodPlanks, 4}),
    shaped(RecipeCategory::Material, 1, 2, {ItemId::WoodPlanks, ItemId::WoodPlanks}, {ItemId::Stick, 4}),
    shaped(RecipeCategory::Material, 2, 2,
           {ItemId::WoodPlanks, ItemId::WoodPlanks, ItemId::WoodPlanks, ItemId::WoodPlanks}, {ItemId::CraftingTable, 1}),

    // Sword ladder (craft from scratch).
    shaped(RecipeCategory::WeaponUpgrade, 1, 3, {ItemId::WoodPlanks, ItemId::WoodPlanks, ItemId::Stick},
           {ItemId::WoodenSword, 1}),
    shaped(RecipeCategory::WeaponUpgrade, 1, 3, {ItemId::BlockCobblestone, ItemId::BlockCobblestone, ItemId::Stick},
           {ItemId::StoneSword, 1}),
    shaped(RecipeCategory::WeaponUpgrade, 1, 3, {ItemId::IronIngot, ItemId::IronIngot, ItemId::Stick},
           {ItemId::IronSword, 1}),
    shaped(RecipeCategory::WeaponUpgrade, 1, 3, {ItemId::Diamond, ItemId::Diamond, ItemId::Stick},
           {ItemId::DiamondSword, 1}),
    shaped(RecipeCategory::WeaponUpgrade, 1, 3, {ItemId::NetheriteIngot, ItemId::NetheriteIngot, ItemId::Stick},
           {ItemId::NetheriteSword, 1}),

    // Tactical items.
    shaped(RecipeCategory::Tactical, 3, 3,
           {ItemId::WoodPlanks, ItemId::IronIngot, ItemId::WoodPlanks, ItemId::WoodPlanks, ItemId::WoodPlanks,
            ItemId::WoodPlanks, kNone, ItemId::WoodPlanks, kNone},
           {ItemId::Shield, 1}),
    shaped(RecipeCategory::Tactical, 3, 3,
           {ItemId::IronIngot, ItemId::IronIngot, ItemId::IronIngot, kNone, ItemId::IronIngot, kNone, ItemId::IronIngot,
            ItemId::IronIngot, ItemId::IronIngot},
           {ItemId::Anvil, 1}),
    shaped(RecipeCategory::Tactical, 3, 3,
           {ItemId::Gunpowder, ItemId::Sand, ItemId::Gunpowder, ItemId::Sand, ItemId::Gunpowder, ItemId::Sand,
            ItemId::Gunpowder, ItemId::Sand, ItemId::Gunpowder},
           {ItemId::BlockTnt, 1}),
    shapeless(RecipeCategory::Tactical, {{ItemId::IronIngot, 1}, {ItemId::Flint, 1}}, {ItemId::FlintAndSteel, 1}),
    shaped(RecipeCategory::Tactical, 3, 3,
           {ItemId::GoldIngot, ItemId::GoldIngot, ItemId::GoldIngot, ItemId::GoldIngot, ItemId::Apple, ItemId::GoldIngot,
            ItemId::GoldIngot, ItemId::GoldIngot, ItemId::GoldIngot},
           {ItemId::GoldenApple, 1}),
}};

} // namespace

const ItemStack& CraftingGrid::at(int x, int y) const {
    static const ItemStack kEmpty{};
    if (x < 0 || x >= kCraftGridSize || y < 0 || y >= kCraftGridSize) return kEmpty;
    return cells[static_cast<size_t>(y * kCraftGridSize + x)];
}

ItemStack& CraftingGrid::at(int x, int y) {
    const int cx = std::clamp(x, 0, kCraftGridSize - 1);
    const int cy = std::clamp(y, 0, kCraftGridSize - 1);
    return cells[static_cast<size_t>(cy * kCraftGridSize + cx)];
}

bool CraftingGrid::empty() const {
    return std::all_of(cells.begin(), cells.end(), [](const ItemStack& stack) { return stack.empty(); });
}

void CraftingGrid::clear() {
    for (ItemStack& cell : cells) cell = {};
}

int CraftingGrid::occupied() const {
    return static_cast<int>(std::count_if(cells.begin(), cells.end(), [](const ItemStack& stack) { return !stack.empty(); }));
}

std::span<const Recipe> defaultRecipes() noexcept {
    return kRecipes;
}

RecipeBook::RecipeBook(std::span<const Recipe> recipes) : recipes_(recipes) {}

bool RecipeBook::matches(const Recipe& r, const CraftingGrid& g) const {
    return r.kind == RecipeKind::Shaped ? matchesShaped(r, g) : matchesShapeless(r, g);
}

const Recipe* RecipeBook::find(const CraftingGrid& g) const {
    for (const Recipe& r : recipes_) {
        if (matches(r, g)) return &r;
    }
    return nullptr;
}

std::vector<const Recipe*> RecipeBook::craftable(const CraftingGrid& g) const {
    std::vector<const Recipe*> found;
    for (const Recipe& r : recipes_) {
        if (matches(r, g)) found.push_back(&r);
    }
    return found;
}

CraftResult RecipeBook::craft(const Recipe& r, CraftingGrid& g, Inventory& inv) const {
    CraftResult out;
    if (!matches(r, g)) return out;
    if (r.result.empty() || !inv.canAdd(r.result)) return out;

    for (ItemStack& cell : g.cells) {
        if (cell.empty()) continue;
        if (--cell.count == 0) cell = {};
    }
    inv.add(r.result);
    out.ok = true;
    out.output = r.result;
    return out;
}

CraftResult RecipeBook::quickCraft(const Recipe& r, Inventory& inv) const {
    CraftResult out;
    std::array<ItemId, kCraftCellCount> needed{};
    const int needed_count = expandIngredients(r, needed);
    if (needed_count == 0 || r.result.empty()) return out;

    // Work on a copy so a missing ingredient or a full inventory leaves `inv` untouched.
    Inventory simulated = inv;
    for (int i = 0; i < needed_count; ++i) {
        int slot = -1;
        for (int s = 0; s < Inventory::kSlots; ++s) {
            const ItemStack& stack = simulated.slot(s);
            if (!stack.empty() && stack.item == needed[static_cast<size_t>(i)]) {
                slot = s;
                break;
            }
        }
        if (slot < 0) return out;
        simulated.consume(slot);
    }
    if (!simulated.canAdd(r.result)) return out;

    simulated.add(r.result);
    inv = simulated;
    out.ok = true;
    out.output = r.result;
    return out;
}

} // namespace mc
