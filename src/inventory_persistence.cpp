#include "mc/inventory_persistence.hpp"

#include <algorithm>
#include <cstdint>

namespace mc {
namespace {

struct SlugEntry {
    ItemId item;
    const char* slug;
};

// Stable names for the save file. ItemId values are append-only, but names keep a save readable and
// survive a reorder. Keep this in sync with kLastItemId.
constexpr SlugEntry kSlugs[] = {
    {ItemId::DiamondSword, "diamond_sword"},
    {ItemId::DiamondPickaxe, "diamond_pickaxe"},
    {ItemId::Bow, "bow"},
    {ItemId::Arrow, "arrow"},
    {ItemId::Trident, "trident"},
    {ItemId::FlintAndSteel, "flint_and_steel"},
    {ItemId::EnderPearl, "ender_pearl"},
    {ItemId::GoldenApple, "golden_apple"},
    {ItemId::EnchantedGoldenApple, "enchanted_golden_apple"},
    {ItemId::Bread, "bread"},
    {ItemId::CookedBeef, "cooked_beef"},
    {ItemId::TotemOfUndying, "totem_of_undying"},
    {ItemId::Elytra, "elytra"},
    {ItemId::FireworkRocket, "firework_rocket"},
    {ItemId::BlockDirt, "block_dirt"},
    {ItemId::BlockStone, "block_stone"},
    {ItemId::BlockTnt, "block_tnt"},
    {ItemId::ZombieSpawnEgg, "zombie_spawn_egg"},
    {ItemId::CreeperSpawnEgg, "creeper_spawn_egg"},
    {ItemId::SkeletonSpawnEgg, "skeleton_spawn_egg"},
    {ItemId::BowPulling0, "bow_pulling_0"},
    {ItemId::BowPulling1, "bow_pulling_1"},
    {ItemId::BowPulling2, "bow_pulling_2"},
    {ItemId::Shield, "shield"},
    {ItemId::DiamondHelmet, "diamond_helmet"},
    {ItemId::DiamondChestplate, "diamond_chestplate"},
    {ItemId::DiamondLeggings, "diamond_leggings"},
    {ItemId::DiamondBoots, "diamond_boots"},
    {ItemId::Coal, "coal"},
    {ItemId::RawCopper, "raw_copper"},
    {ItemId::RawIron, "raw_iron"},
    {ItemId::Redstone, "redstone"},
    {ItemId::RawGold, "raw_gold"},
    {ItemId::LapisLazuli, "lapis_lazuli"},
    {ItemId::AmethystShard, "amethyst_shard"},
    {ItemId::Diamond, "diamond"},
    {ItemId::Emerald, "emerald"},
    {ItemId::NetheriteScrap, "netherite_scrap"},
    {ItemId::BlockCobblestone, "block_cobblestone"},
    {ItemId::WoodenPickaxe, "wooden_pickaxe"},
    {ItemId::StonePickaxe, "stone_pickaxe"},
    {ItemId::IronPickaxe, "iron_pickaxe"},
    {ItemId::GoldenPickaxe, "golden_pickaxe"},
    {ItemId::NetheritePickaxe, "netherite_pickaxe"},
    {ItemId::BlockWood, "block_wood"},
    {ItemId::BlockObsidian, "block_obsidian"},
    {ItemId::WoodenSword, "wooden_sword"},
    {ItemId::StoneSword, "stone_sword"},
    {ItemId::IronSword, "iron_sword"},
    {ItemId::NetheriteSword, "netherite_sword"},
    {ItemId::CraftingTable, "crafting_table"},
    {ItemId::Anvil, "anvil"},
    {ItemId::WoodPlanks, "wood_planks"},
    {ItemId::Stick, "stick"},
    {ItemId::IronIngot, "iron_ingot"},
    {ItemId::GoldIngot, "gold_ingot"},
    {ItemId::NetheriteIngot, "netherite_ingot"},
    {ItemId::String, "string"},
    {ItemId::Gunpowder, "gunpowder"},
    {ItemId::Sand, "sand"},
    {ItemId::Flint, "flint"},
    {ItemId::Apple, "apple"},
    {ItemId::BlockGravel, "block_gravel"},
    {ItemId::BlockStoneBricks, "block_stone_bricks"},
    {ItemId::BlockBricks, "block_bricks"},
    {ItemId::BlockWhiteWool, "block_white_wool"},
    {ItemId::BlockGlass, "block_glass"},
    {ItemId::BlockLeaves, "block_leaves"},
    {ItemId::BlockStoneSlab, "block_stone_slab"},
    {ItemId::Torch, "torch"},
};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

bool parseUint(std::string_view s, uint32_t& out) {
    if (s.empty()) return false;
    uint64_t value = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > 0xFFFFFFFFull) return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool parseIndex(std::string_view key, std::string_view prefix, int limit, int& out) {
    if (key.size() <= prefix.size() || key.substr(0, prefix.size()) != prefix) return false;
    uint32_t value = 0;
    if (!parseUint(key.substr(prefix.size()), value)) return false;
    if (value >= static_cast<uint32_t>(limit)) return false;
    out = static_cast<int>(value);
    return true;
}

// "slug:count", or "slug" for one item.
bool parseStack(std::string_view s, ItemStack& out) {
    const size_t colon = s.find(':');
    const std::string_view slug = trim(s.substr(0, colon));
    uint32_t count = 1;
    if (colon != std::string_view::npos && !parseUint(trim(s.substr(colon + 1)), count)) return false;
    if (count == 0) return false;
    const ItemId item = itemFromSlug(slug);
    if (item == ItemId::None) return false;
    const uint32_t cap = maxStackSize(item);
    if (cap == 0) return false;
    out = {item, std::min(count, cap)};
    return true;
}

void appendStack(std::string& out, std::string_view key, const ItemStack& s) {
    if (s.empty()) return;
    const std::string_view slug = itemSlug(s.item);
    if (slug.empty()) return;
    out += key;
    out += '=';
    out.append(slug.data(), slug.size());
    out += ':';
    out += std::to_string(s.count);
    out += '\n';
}

} // namespace

std::string_view itemSlug(ItemId item) {
    for (const SlugEntry& e : kSlugs) {
        if (e.item == item) return e.slug;
    }
    return {};
}

ItemId itemFromSlug(std::string_view slug) {
    for (const SlugEntry& e : kSlugs) {
        if (slug == e.slug) return e.item;
    }
    return ItemId::None;
}

std::string serializeInventory(const Inventory& inv, int selected_slot, const CraftingGrid& grid) {
    std::string out;
    out += "mc-inventory ";
    out += std::to_string(kInventorySaveVersion);
    out += '\n';
    out += "selected=";
    out += std::to_string(std::clamp(selected_slot, 0, Inventory::kHotbar - 1));
    out += '\n';
    for (int i = 0; i < Inventory::kSlots; ++i) appendStack(out, "slot." + std::to_string(i), inv.slot(i));
    for (int i = 0; i < Inventory::kArmorSlots; ++i) appendStack(out, "armor." + std::to_string(i), inv.armor(i));
    appendStack(out, "offhand", inv.offhand());
    appendStack(out, "cursor", inv.cursor());
    for (int i = 0; i < kCraftCellCount; ++i) {
        appendStack(out, "grid." + std::to_string(i), grid.cells[static_cast<size_t>(i)]);
    }
    return out;
}

InventoryLoadResult deserializeInventory(std::string_view text, Inventory& inv, int& selected_slot, CraftingGrid& grid) {
    InventoryLoadResult result;
    Inventory parsed;
    CraftingGrid parsed_grid;
    int parsed_selected = 0;
    bool header = false;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = trim(text.substr(pos, end - pos));
        pos = end + 1;

        if (line.empty()) continue;
        if (!header) {
            const size_t space = line.find(' ');
            if (line.substr(0, space) != "mc-inventory") {
                result.error = "missing header";
                return result;
            }
            uint32_t version = 0;
            if (space == std::string_view::npos || !parseUint(trim(line.substr(space + 1)), version)) {
                result.error = "missing version";
                return result;
            }
            if (version == 0 || version > static_cast<uint32_t>(kInventorySaveVersion)) {
                result.error = "unsupported version " + std::to_string(version);
                return result;
            }
            header = true;
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            ++result.skipped;
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));

        if (key == "selected") {
            uint32_t slot = 0;
            if (parseUint(value, slot)) parsed_selected = static_cast<int>(std::min<uint32_t>(slot, Inventory::kHotbar - 1));
            else ++result.skipped;
            continue;
        }

        int index = 0;
        if (parseIndex(key, "slot.", Inventory::kSlots, index)) {
            ItemStack s;
            if (!parseStack(value, s)) {
                ++result.skipped;
                continue;
            }
            parsed.setSlot(index, s);
            ++result.loaded;
            continue;
        }
        if (parseIndex(key, "armor.", Inventory::kArmorSlots, index)) {
            ItemStack s;
            if (!parseStack(value, s) || getArmorSlot(s.item) != static_cast<ArmorSlot>(index)) {
                ++result.skipped;
                continue;
            }
            parsed.setArmor(index, s);
            ++result.loaded;
            continue;
        }
        if (parseIndex(key, "grid.", kCraftCellCount, index)) {
            ItemStack s;
            if (!parseStack(value, s)) {
                ++result.skipped;
                continue;
            }
            parsed_grid.cells[static_cast<size_t>(index)] = s;
            ++result.loaded;
            continue;
        }
        if (key == "offhand" || key == "cursor") {
            ItemStack s;
            if (!parseStack(value, s)) {
                ++result.skipped;
                continue;
            }
            if (key == "offhand") parsed.setOffhand(s);
            else parsed.setCursor(s);
            ++result.loaded;
        }
        // Unknown keys are ignored so a newer save still loads.
    }

    if (!header) {
        result.error = "empty file";
        return result;
    }
    inv = parsed;
    grid = parsed_grid;
    selected_slot = parsed_selected;
    result.ok = true;
    return result;
}

} // namespace mc
