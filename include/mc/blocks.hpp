#pragma once

#include "mc/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mc {

struct Cell {
    int x{0}, y{0}, z{0};
    bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct CellHash {
    size_t operator()(const Cell& c) const noexcept {
        return (static_cast<size_t>(static_cast<uint32_t>(c.x)) * 73856093u) ^ (static_cast<size_t>(static_cast<uint32_t>(c.y)) * 19349663u) ^
               (static_cast<size_t>(static_cast<uint32_t>(c.z)) * 83492791u);
    }
};

inline Cell cellOf(float x, float y, float z) {
    return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
}

inline mc::BlockId blockForItem(mc::ItemId item) {
    switch (item) {
        case mc::ItemId::BlockDirt: return mc::BlockId::Dirt;
        case mc::ItemId::BlockStone: return mc::BlockId::Stone;
        case mc::ItemId::BlockTnt: return mc::BlockId::Tnt;
        case mc::ItemId::BlockCobblestone: return mc::BlockId::Cobblestone;
        case mc::ItemId::BlockWood: return mc::BlockId::Wood;
        case mc::ItemId::BlockObsidian: return mc::BlockId::Obsidian;
        case mc::ItemId::CraftingTable: return mc::BlockId::CraftingTable;
        case mc::ItemId::Sand: return mc::BlockId::Sand;
        case mc::ItemId::BlockGravel: return mc::BlockId::Gravel;
        case mc::ItemId::WoodPlanks: return mc::BlockId::WoodPlanks;
        case mc::ItemId::BlockStoneBricks: return mc::BlockId::StoneBricks;
        case mc::ItemId::BlockBricks: return mc::BlockId::Bricks;
        case mc::ItemId::BlockWhiteWool: return mc::BlockId::WhiteWool;
        case mc::ItemId::Anvil: return mc::BlockId::Anvil;
        default: return mc::BlockId::Air;
    }
}

inline mc::ItemId itemForBlock(mc::BlockId block) {
    switch (block) {
        case mc::BlockId::Dirt: return mc::ItemId::BlockDirt;
        case mc::BlockId::Stone: return mc::ItemId::BlockStone;
        case mc::BlockId::Tnt: return mc::ItemId::BlockTnt;
        case mc::BlockId::Cobblestone: return mc::ItemId::BlockCobblestone;
        case mc::BlockId::Wood: return mc::ItemId::BlockWood;
        case mc::BlockId::Obsidian: return mc::ItemId::BlockObsidian;
        case mc::BlockId::CraftingTable: return mc::ItemId::CraftingTable;
        case mc::BlockId::Sand: return mc::ItemId::Sand;
        case mc::BlockId::Gravel: return mc::ItemId::BlockGravel;
        case mc::BlockId::WoodPlanks: return mc::ItemId::WoodPlanks;
        case mc::BlockId::StoneBricks: return mc::ItemId::BlockStoneBricks;
        case mc::BlockId::Bricks: return mc::ItemId::BlockBricks;
        case mc::BlockId::WhiteWool: return mc::ItemId::BlockWhiteWool;
        case mc::BlockId::Anvil: return mc::ItemId::Anvil;
        default: return mc::ItemId::None;
    }
}

class BlockGrid {
public:
    static constexpr int kMaxBlocks = 2048; // the mesh is rebuilt on every change: keep it small

    bool place(const Cell& c, mc::BlockId id) {
        if (id == mc::BlockId::Air || blocks_.count(c) != 0 || static_cast<int>(blocks_.size()) >= kMaxBlocks) return false;
        blocks_[c] = id;
        ++version_;
        return true;
    }

    bool remove(const Cell& c) {
        if (blocks_.erase(c) == 0) return false;
        ++version_;
        return true;
    }

    [[nodiscard]] mc::BlockId get(const Cell& c) const {
        const auto it = blocks_.find(c);
        return it != blocks_.end() ? it->second : mc::BlockId::Air;
    }

    [[nodiscard]] size_t size() const { return blocks_.size(); }
    [[nodiscard]] size_t count() const { return blocks_.size(); }
    [[nodiscard]] uint32_t version() const { return version_; }
    [[nodiscard]] const std::unordered_map<Cell, mc::BlockId, CellHash>& all() const { return blocks_; }

    void clear() {
        if (!blocks_.empty()) {
            blocks_.clear();
            ++version_;
        }
    }

    void shift(int dx, int dy, int dz) {
        if (dx == 0 && dy == 0 && dz == 0) return;
        if (blocks_.empty()) return;
        std::unordered_map<Cell, mc::BlockId, CellHash> new_blocks;
        new_blocks.reserve(blocks_.size());
        for (const auto& [cell, id] : blocks_) {
            new_blocks[{cell.x + dx, cell.y + dy, cell.z + dz}] = id;
        }
        blocks_ = std::move(new_blocks);
        ++version_;
    }

private:
    std::unordered_map<Cell, mc::BlockId, CellHash> blocks_;
    uint32_t version_{0};
};

} // namespace mc
