#pragma once

#include "eldenring_blocks.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <cmath>

namespace eldenring::mining {

struct MiningState {
    bool is_mining{false};
    blocks::Cell target_cell{};
    mc::BlockId block_id{mc::BlockId::Air};
    float progress{0.0f}; // 0.0 to 1.0
    int stage{-1};         // -1 = none, 0..9 = crack stage
    bool broken{false};    // true on the exact frame the block breaks
    bool hit_sound{false}; // true when a periodic hit sound should be played
};

class MiningController {
public:
    static float computeBreakTime(mc::BlockId block_id, mc::ItemId held_tool, bool is_creative) {
        if (is_creative) {
            return 0.0f; // Instant break in creative mode
        }
        if (block_id == mc::BlockId::Tnt) {
            return 0.05f; // Extremely fast / near-instant break
        }
        if (block_id == mc::BlockId::Stone) {
            return (held_tool == mc::ItemId::DiamondPickaxe) ? 0.3f : 7.5f;
        }
        if (block_id == mc::BlockId::Dirt) {
            return 0.75f;
        }
        return 1.0f;
    }

    static bool canHarvest(mc::BlockId block_id, mc::ItemId held_tool, bool is_creative) {
        if (is_creative) {
            return false; // Creative mode breaks blocks without dropping items
        }
        if (block_id == mc::BlockId::Stone) {
            return held_tool == mc::ItemId::DiamondPickaxe;
        }
        if (block_id == mc::BlockId::Dirt || block_id == mc::BlockId::Tnt) {
            return true;
        }
        return false;
    }

    void update(float dt, bool left_down, bool has_target, const blocks::Cell& target_cell,
                mc::BlockId target_block, mc::ItemId held_tool, bool is_creative) {
        state_.broken = false;
        state_.hit_sound = false;

        if (!left_down || !has_target || target_block == mc::BlockId::Air) {
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
        state_.block_id = mc::BlockId::Air;
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

} // namespace eldenring::mining
