#pragma once

// Block entities: the per-cell state that a flat block grid cannot carry -- chest contents, bed
// halves, banner patterns, skull owners and piston state. `BlockId` (mc/types.hpp) stays a plain
// block-type grid and is deliberately not extended with chest/bed/piston ids: a
// `BlockEntityRegistry` keyed by `mc::Cell` holds this state beside the grid, and an adapter pairs
// each block entity with whatever visual `BlockId` it chooses. Pure logic, no engine or game access.

#include "mc/blocks.hpp"    // Cell, CellHash, BlockGrid
#include "mc/inventory.hpp" // ItemStack, maxStackSize
#include "mc/types.hpp"     // BlockId, ItemId

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace mc {

enum class BlockEntityKind : uint8_t { Chest, TrappedChest, ShulkerBox, Bed, Banner, Skull, Piston };

// The 16 vanilla dye colours, in the game's own order.
enum class DyeColor : uint8_t {
    White,
    Orange,
    Magenta,
    LightBlue,
    Yellow,
    Lime,
    Pink,
    Gray,
    LightGray,
    Cyan,
    Purple,
    Blue,
    Brown,
    Green,
    Red,
    Black,
    Count
};
inline constexpr int kDyeColorCount = static_cast<int>(DyeColor::Count);

// A single chest, trapped chest or shulker box: 27 slots with vanilla stacking rules.
class Container {
public:
    static constexpr int kSlots = 27;

    // Clamps into range, like Inventory::slot.
    [[nodiscard]] const ItemStack& get(int slot) const;
    [[nodiscard]] ItemStack& get(int slot);
    // False when the slot is out of range. An empty stack clears the slot; the count is clamped
    // to maxStackSize().
    bool set(int slot, ItemStack stack);

    // Puts as much of `stack` in as fits: partial stacks of the same item first, then empty slots.
    // Returns what did not fit (an empty stack when everything went in).
    ItemStack insert(ItemStack stack);
    // Takes up to `count` items out of one slot and returns them; the slot empties at zero.
    ItemStack remove(int slot, uint32_t count);

    [[nodiscard]] int emptySlots() const;
    [[nodiscard]] uint32_t count(ItemId item) const;
    [[nodiscard]] bool empty() const;
    void clear();

private:
    std::array<ItemStack, kSlots> slots_{};
};

// Vanilla slot clicks on a container, with the cursor living in the player's inventory (the same
// rules Inventory::click uses for its own slots). Shift moves the stack between the container and
// the player's 36 slots.
void containerClick(Container& container, int slot, Button button, bool shift, Inventory& player);
// Shift-click on a player inventory slot while a container screen is open: move it into the container.
void playerShiftClickIntoContainer(Container& container, Inventory& player, int slot);

enum class BedHalf : uint8_t { Head, Foot };

// A bed: one entity per half, each half knowing both cells so the pair can be resolved either way.
class Bed {
public:
    Bed() = default;
    Bed(DyeColor color, BedHalf half, Cell head, Cell foot);

    [[nodiscard]] DyeColor color() const { return color_; }
    void setColor(DyeColor color) { color_ = color; }

    [[nodiscard]] BedHalf half() const { return half_; }
    void setHalf(BedHalf half) { half_ = half; }

    [[nodiscard]] const Cell& head() const { return head_; }
    [[nodiscard]] const Cell& foot() const { return foot_; }
    // False when head and foot are the same cell; the old cells stay.
    bool setCells(Cell head, Cell foot);

    // The partner cell of `from`, or nullopt when `from` is neither half (a third cell).
    [[nodiscard]] std::optional<Cell> otherHalf(const Cell& from) const;

    // True while a player sleeps in the bed; the sleep system owns this flag.
    [[nodiscard]] bool occupied() const { return occupied_; }
    void setOccupied(bool occupied) { occupied_ = occupied; }

private:
    DyeColor color_{DyeColor::White};
    BedHalf half_{BedHalf::Foot};
    Cell head_{};
    Cell foot_{};
    bool occupied_{false};
};

// The vanilla banner patterns (a subset of the 42; enough to cover every layer kind an adapter
// renders). Each layer carries its own dye colour.
enum class BannerPatternKind : uint8_t {
    Border,
    Bricks,
    Circle,
    Creeper,
    Cross,
    CurlyBorder,
    DiagonalUp,
    DiagonalDown,
    Flower,
    Globe,
    Gradient,
    GradientUp,
    HalfHorizontal,
    HalfVertical,
    Mojang,
    Piglin,
    Rhombus,
    Skull,
    SmallStripes,
    SquareBottomLeft,
    SquareBottomRight,
    SquareTopLeft,
    SquareTopRight,
    Straighter,
    StripeBottom,
    StripeCenter,
    StripeDownLeft,
    StripeDownRight,
    StripeLeft,
    StripeMiddle,
    StripeRight,
    StripeTop,
    TriangleBottom,
    TriangleTop,
    TrianglesBottom,
    TrianglesTop
};

struct BannerPattern {
    BannerPatternKind kind{BannerPatternKind::Border};
    DyeColor color{DyeColor::White};

    constexpr bool operator==(const BannerPattern&) const = default;
};

// A banner: one base colour and at most six pattern layers, applied bottom-up.
class Banner {
public:
    static constexpr int kMaxPatterns = 6;

    Banner() = default;
    explicit Banner(DyeColor base) : base_color_(base) {}

    [[nodiscard]] DyeColor baseColor() const { return base_color_; }
    void setBaseColor(DyeColor base) { base_color_ = base; }

    // False once the banner already has six patterns: a seventh layer is refused.
    bool addPattern(BannerPatternKind kind, DyeColor color);
    void clearPatterns();

    [[nodiscard]] int patternCount() const { return pattern_count_; }
    [[nodiscard]] std::span<const BannerPattern> patterns() const {
        return {patterns_.data(), static_cast<size_t>(pattern_count_)};
    }

private:
    DyeColor base_color_{DyeColor::White};
    std::array<BannerPattern, kMaxPatterns> patterns_{};
    int pattern_count_{0};
};

enum class SkullKind : uint8_t { Skeleton, WitherSkeleton, Zombie, Player, Creeper, Dragon };

// A head or skull. Only a Player skull carries an owner name.
class Skull {
public:
    Skull() = default;
    explicit Skull(SkullKind kind) : kind_(kind) {}

    [[nodiscard]] SkullKind kind() const { return kind_; }
    // Switching away from Player drops the owner: it only means something for player heads.
    void setKind(SkullKind kind);

    [[nodiscard]] const std::string& owner() const { return owner_; }
    // False for an empty name or for any kind other than Player.
    bool setOwner(std::string name);
    void clearOwner() { owner_.clear(); }

private:
    SkullKind kind_{SkullKind::Skeleton};
    std::string owner_{};
};

enum class Direction : uint8_t { Down, Up, North, South, West, East };

inline constexpr Cell directionOffset(Direction direction) {
    switch (direction) {
        case Direction::Down: return {0, -1, 0};
        case Direction::Up: return {0, 1, 0};
        case Direction::North: return {0, 0, -1};
        case Direction::South: return {0, 0, 1};
        case Direction::West: return {-1, 0, 0};
        case Direction::East: return {1, 0, 0};
    }
    return {0, 0, 0};
}

inline constexpr Cell offsetCell(const Cell& cell, const Cell& offset) {
    return {cell.x + offset.x, cell.y + offset.y, cell.z + offset.z};
}

// A piston or sticky piston. The base occupies its own cell; the head, when extended, occupies the
// neighbouring cell in `direction()`.
class Piston {
public:
    Piston() = default;
    Piston(Direction direction, bool sticky) : direction_(direction), sticky_(sticky) {}

    [[nodiscard]] Direction direction() const { return direction_; }
    void setDirection(Direction direction) { direction_ = direction; }

    [[nodiscard]] bool sticky() const { return sticky_; }
    void setSticky(bool sticky) { sticky_ = sticky; }

    [[nodiscard]] bool extended() const { return extended_; }
    void setExtended(bool extended) { extended_ = extended; }

    [[nodiscard]] Cell headCell(const Cell& base) const { return offsetCell(base, directionOffset(direction_)); }

private:
    Direction direction_{Direction::Up};
    bool sticky_{false};
    bool extended_{false};
};

enum class PistonAction : uint8_t { Extend, Retract };

// What a piston would move. `moved` lists the occupied cells that travel, nearest to the piston
// first; it also keeps the cells examined so far when the move is refused, so a caller can report
// what blocked it. `pulled` is the single block a sticky piston drags back on retraction.
struct PistonMove {
    bool possible{false};
    PistonAction action{PistonAction::Extend};
    std::vector<Cell> moved{};
    std::optional<Cell> pulled{};
};

class PistonRules {
public:
    static constexpr int kMaxPush = 12; // vanilla: a piston moves at most 12 blocks

    // Blocks a piston cannot move: obsidian and anvils in vanilla. Air is not immovable, it is
    // pass-through (see isPassThrough).
    [[nodiscard]] static constexpr bool isImmovable(BlockId id) {
        switch (id) {
            case BlockId::Obsidian:
            case BlockId::Anvil: return true;
            default: return false;
        }
    }

    // Cells a piston pushes through without moving anything. Air today; fluids join this set once
    // core models them.
    [[nodiscard]] static constexpr bool isPassThrough(BlockId id) { return id == BlockId::Air; }

    // Extend: walk the line in front of the piston. The move is possible when a pass-through cell
    // is reached within kMaxPush blocks and no immovable block is in the way.
    // Retract: always possible; a sticky piston additionally pulls back exactly one block, the one
    // against its head.
    [[nodiscard]] static PistonMove planMove(const BlockGrid& grid, const Cell& piston_cell, Direction direction,
                                             bool sticky, PistonAction action = PistonAction::Extend);
};

using BlockEntityPayload = std::variant<Container, Bed, Banner, Skull, Piston>;

// One cell's block entity. `kind` says which payload alternative is meaningful; the registry
// constructs the matching one.
struct BlockEntity {
    BlockEntityKind kind{BlockEntityKind::Chest};
    BlockEntityPayload payload{};
};

// The block entities of a world, keyed by cell. At most one per cell. `version()` bumps on every
// membership change so a renderer or a mesh builder can tell when to rebuild.
class BlockEntityRegistry {
public:
    // The new entity, or nullptr when the cell already holds one.
    BlockEntity* create(const Cell& cell, BlockEntityKind kind);
    bool remove(const Cell& cell);

    [[nodiscard]] BlockEntity* get(const Cell& cell);
    [[nodiscard]] const BlockEntity* get(const Cell& cell) const;
    [[nodiscard]] bool contains(const Cell& cell) const;

    // The payload of the matching type, or nullptr when the cell is empty or holds another kind.
    template <typename T>
    [[nodiscard]] T* getAs(const Cell& cell) {
        BlockEntity* entity = get(cell);
        return entity != nullptr ? std::get_if<T>(&entity->payload) : nullptr;
    }
    template <typename T>
    [[nodiscard]] const T* getAs(const Cell& cell) const {
        const BlockEntity* entity = get(cell);
        return entity != nullptr ? std::get_if<T>(&entity->payload) : nullptr;
    }

    [[nodiscard]] const std::unordered_map<Cell, BlockEntity, CellHash>& all() const { return entities_; }
    [[nodiscard]] uint32_t version() const { return version_; }

    // Bumps version() after an in-place payload edit (a container insert, a bed occupancy change);
    // the registry cannot see those through get().
    void touch(const Cell& cell);

    void clear();

private:
    std::unordered_map<Cell, BlockEntity, CellHash> entities_{};
    uint32_t version_{0};
};

} // namespace mc
