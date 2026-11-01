#pragma once

// Minecraft progressive block destruction and mining hardness system.
// Computes breakdown duration based on block type and held tool, 10-stage crack overlays, and hit sound timing.

#include "mc/loot_table.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <cmath>

namespace mc {

struct MiningState {
    bool is_mining{false};
    GridPos target_cell{};
    BlockId block_id{BlockId::Air};
    float progress{0.0f}; // 0.0 to 1.0
    int stage{-1};         // -1 = none, 0..9 = crack stage
    bool broken{false};    // true on the exact frame the block breaks
    bool hit_sound{false}; // true when a periodic hit sound should be played
};

class MiningController {
public:
    static float computeBreakTime(BlockId block_id, ItemId held_tool, bool is_creative) {
        if (is_creative) {
            return 0.0f; // Instant break in creative mode
        }
        if (block_id == BlockId::Tnt) {
            return 0.05f; // Extremely fast / near-instant break
        }
        if (block_id == BlockId::Wood || block_id == BlockId::CraftingTable || block_id == BlockId::WoodPlanks) {
            return held_tool == ItemId::None ? 3.0f : 1.5f; // no axes in the mod: any tool speeds it up
        }
        if (block_id == BlockId::Obsidian) {
            if (held_tool == ItemId::DiamondPickaxe) return 9.4f;
            if (held_tool == ItemId::NetheritePickaxe) return 8.35f;
            return 30.0f; // cannot be harvested without diamond/netherite
        }
        if (block_id == BlockId::Stone || block_id == BlockId::Cobblestone || block_id == BlockId::StoneBricks ||
            block_id == BlockId::Bricks) {
            switch (toolTier(held_tool)) {
                case ToolTier::Wood: return 1.15f;
                case ToolTier::Stone: return 0.6f;
                case ToolTier::Iron: return 0.4f;
                case ToolTier::Gold: return 0.2f;
                case ToolTier::Diamond: return 0.3f;
                case ToolTier::Netherite: return 0.25f;
                default: return 7.5f;
            }
        }
        if (block_id == BlockId::Anvil) {
            switch (toolTier(held_tool)) {
                case ToolTier::Wood: return 3.8f;
                case ToolTier::Stone: return 2.0f;
                case ToolTier::Iron: return 1.35f;
                case ToolTier::Gold: return 0.7f;
                case ToolTier::Diamond: return 1.0f;
                case ToolTier::Netherite: return 0.85f;
                default: return 25.0f;
            }
        }
        if (block_id == BlockId::Dirt || block_id == BlockId::Sand || block_id == BlockId::Gravel) {
            return 0.75f;
        }
        if (block_id == BlockId::WhiteWool) {
            return 0.8f;
        }
        return 1.0f;
    }

    static bool canHarvest(BlockId block_id, ItemId held_tool, bool is_creative) {
        if (is_creative) {
            return false; // Creative mode breaks blocks without dropping items
        }
        if (block_id == BlockId::Stone || block_id == BlockId::Cobblestone || block_id == BlockId::StoneBricks ||
            block_id == BlockId::Bricks || block_id == BlockId::Anvil) {
            return isPickaxe(held_tool);
        }
        if (block_id == BlockId::Dirt || block_id == BlockId::Tnt || block_id == BlockId::Wood ||
            block_id == BlockId::CraftingTable || block_id == BlockId::Sand || block_id == BlockId::Gravel ||
            block_id == BlockId::WoodPlanks || block_id == BlockId::WhiteWool) {
            return true;
        }
        if (block_id == BlockId::Obsidian) {
            return held_tool == ItemId::DiamondPickaxe || held_tool == ItemId::NetheritePickaxe;
        }
        return false;
    }

    // The item a broken placed block leaves behind. Stone drops cobblestone as in vanilla; wood
    // and obsidian have no item ids yet, so they drop nothing until those are added.
    [[nodiscard]] static ItemId blockDrop(BlockId block_id, ItemId held_tool, bool is_creative) noexcept {
        if (is_creative) return ItemId::None;
        switch (block_id) {
            case BlockId::Stone: return isPickaxe(held_tool) ? ItemId::BlockCobblestone : ItemId::None;
            case BlockId::Cobblestone: return isPickaxe(held_tool) ? ItemId::BlockCobblestone : ItemId::None;
            case BlockId::Dirt: return ItemId::BlockDirt;
            case BlockId::Tnt: return ItemId::BlockTnt;
            case BlockId::Wood: return ItemId::BlockWood;
            case BlockId::CraftingTable: return ItemId::CraftingTable;
            case BlockId::Sand: return ItemId::Sand;
            case BlockId::Gravel: return ItemId::BlockGravel;
            case BlockId::WoodPlanks: return ItemId::WoodPlanks;
            case BlockId::StoneBricks: return isPickaxe(held_tool) ? ItemId::BlockStoneBricks : ItemId::None;
            case BlockId::Bricks: return isPickaxe(held_tool) ? ItemId::BlockBricks : ItemId::None;
            case BlockId::WhiteWool: return ItemId::BlockWhiteWool;
            case BlockId::Anvil: return isPickaxe(held_tool) ? ItemId::Anvil : ItemId::None;
            case BlockId::Obsidian:
                return (held_tool == ItemId::DiamondPickaxe || held_tool == ItemId::NetheritePickaxe) ? ItemId::BlockObsidian : ItemId::None;
            default: return ItemId::None;
        }
    }

    void update(float dt, bool left_down, bool has_target, const GridPos& target_cell,
                BlockId target_block, ItemId held_tool, bool is_creative) {
        state_.broken = false;
        state_.hit_sound = false;

        if (!left_down || !has_target || target_block == BlockId::Air) {
            reset();
            return;
        }

        // If target cell changed, reset progress to start mining the new block
        if (state_.is_mining && !(state_.target_cell == target_cell)) {
            reset();
        }

        const float break_time = computeBreakTime(target_block, held_tool, is_creative);

        if (!state_.is_mining) {
            state_.is_mining = true;
            state_.target_cell = target_cell;
            state_.block_id = target_block;
            state_.progress = 0.0f;
            state_.stage = 0;
            state_.hit_sound = true; // Initial strike sound
            hit_timer_ = 0.0f;
        }

        if (break_time <= 0.001f) {
            state_.progress = 1.0f;
            state_.stage = 9;
            state_.broken = true;
            state_.is_mining = false;
            return;
        }

        state_.progress += (dt / break_time);

        if (state_.progress >= 1.0f) {
            state_.broken = true;
            state_.stage = 9;
            state_.is_mining = false;
            state_.progress = 1.0f;
            return;
        }

        state_.stage = std::clamp(static_cast<int>(state_.progress * 10.0f), 0, 9);

        hit_timer_ += dt;
        if (hit_timer_ >= 0.25f) {
            state_.hit_sound = true;
            hit_timer_ = std::fmod(hit_timer_, 0.25f);
        }
    }

    void reset() {
        state_.is_mining = false;
        state_.target_cell = {};
        state_.block_id = BlockId::Air;
        state_.progress = 0.0f;
        state_.stage = -1;
        state_.broken = false;
        state_.hit_sound = false;
        hit_timer_ = 0.0f;
    }

    [[nodiscard]] const MiningState& state() const { return state_; }

private:
    MiningState state_;
    float hit_timer_{0.0f};
};

} // namespace mc
