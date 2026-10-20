#pragma once

// Forwarding wrapper for mc::MiningController to maintain backward compatibility with eldenring::blocks::Cell.
#include "eldenring_blocks.hpp"
#include "mc/mining.hpp"

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
        return mc::MiningController::computeBreakTime(block_id, held_tool, is_creative);
    }

    static bool canHarvest(mc::BlockId block_id, mc::ItemId held_tool, bool is_creative) {
        return mc::MiningController::canHarvest(block_id, held_tool, is_creative);
    }

    static mc::ItemId blockDrop(mc::BlockId block_id, mc::ItemId held_tool, bool is_creative) {
        return mc::MiningController::blockDrop(block_id, held_tool, is_creative);
    }

    void update(float dt, bool left_down, bool has_target, const blocks::Cell& target_cell,
                mc::BlockId target_block, mc::ItemId held_tool, bool is_creative) {
        mc_controller_.update(dt, left_down, has_target, {target_cell.x, target_cell.y, target_cell.z},
                              target_block, held_tool, is_creative);
        const auto& s = mc_controller_.state();
        state_.is_mining = s.is_mining;
        state_.target_cell = {s.target_cell.x, s.target_cell.y, s.target_cell.z};
        state_.block_id = s.block_id;
        state_.progress = s.progress;
        state_.stage = s.stage;
        state_.broken = s.broken;
        state_.hit_sound = s.hit_sound;
    }

    void reset() {
        mc_controller_.reset();
        state_ = {};
    }

    [[nodiscard]] const MiningState& state() const { return state_; }

private:
    mc::MiningController mc_controller_;
    MiningState state_;
};

} // namespace eldenring::mining
