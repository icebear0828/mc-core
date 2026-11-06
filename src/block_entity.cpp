#include "mc/block_entity.hpp"

#include <algorithm>
#include <utility>

namespace mc {

namespace {

BlockEntityPayload defaultPayload(BlockEntityKind kind) {
    switch (kind) {
        case BlockEntityKind::Chest:
        case BlockEntityKind::TrappedChest:
        case BlockEntityKind::ShulkerBox: return Container{};
        case BlockEntityKind::Bed: return Bed{};
        case BlockEntityKind::Banner: return Banner{};
        case BlockEntityKind::Skull: return Skull{};
        case BlockEntityKind::Piston: return Piston{};
    }
    return Container{};
}

} // namespace

// --- Container -------------------------------------------------------------------------------

const ItemStack& Container::get(int slot) const {
    return slots_[static_cast<size_t>(std::clamp(slot, 0, kSlots - 1))];
}

ItemStack& Container::get(int slot) {
    return slots_[static_cast<size_t>(std::clamp(slot, 0, kSlots - 1))];
}

bool Container::set(int slot, ItemStack stack) {
    if (slot < 0 || slot >= kSlots) return false;
    ItemStack& target = slots_[static_cast<size_t>(slot)];
    if (stack.empty()) {
        target = {};
        return true;
    }
    const uint32_t cap = maxStackSize(stack.item);
    if (cap == 0) {
        target = {};
        return true;
    }
    stack.count = std::min(stack.count, cap);
    target = stack;
    return true;
}

ItemStack Container::insert(ItemStack stack) {
    if (stack.empty()) return {};
    const uint32_t cap = maxStackSize(stack.item);
    if (cap == 0) return stack;

    for (ItemStack& slot : slots_) {
        if (slot.empty() || slot.item != stack.item) continue;
        const uint32_t room = cap > slot.count ? cap - slot.count : 0;
        const uint32_t move = std::min(room, stack.count);
        slot.count += move;
        stack.count -= move;
        if (stack.count == 0) return {};
    }
    for (ItemStack& slot : slots_) {
        if (!slot.empty()) continue;
        const uint32_t move = std::min(cap, stack.count);
        slot = {stack.item, move};
        stack.count -= move;
        if (stack.count == 0) return {};
    }
    return stack;
}

ItemStack Container::remove(int slot, uint32_t count) {
    if (slot < 0 || slot >= kSlots || count == 0) return {};
    ItemStack& source = slots_[static_cast<size_t>(slot)];
    if (source.empty()) return {};
    const uint32_t taken = std::min(count, source.count);
    const ItemStack out{source.item, taken};
    source.count -= taken;
    if (source.count == 0) source = {};
    return out;
}

int Container::emptySlots() const {
    int empty = 0;
    for (const ItemStack& slot : slots_) {
        if (slot.empty()) ++empty;
    }
    return empty;
}

uint32_t Container::count(ItemId item) const {
    uint32_t total = 0;
    for (const ItemStack& slot : slots_) {
        if (!slot.empty() && slot.item == item) total += slot.count;
    }
    return total;
}

bool Container::empty() const { return emptySlots() == kSlots; }

void Container::clear() { slots_ = {}; }

// --- Container clicks ------------------------------------------------------------------------

void containerClick(Container& container, int slot, Button button, bool shift, Inventory& player) {
    if (slot < 0 || slot >= Container::kSlots) return;
    if (shift) {
        ItemStack stack = container.remove(slot, container.get(slot).count);
        if (stack.empty()) return;
        const uint32_t left = player.add(stack);
        if (left > 0) container.set(slot, {stack.item, left});
        return;
    }
    ItemStack& target = container.get(slot);
    ItemStack cursor = player.takeCursor();
    if (button == Button::Left) {
        if (cursor.empty()) {
            cursor = target;
            target = {};
        } else if (target.empty()) {
            target = cursor;
            cursor = {};
        } else if (target.item == cursor.item) {
            const uint32_t cap = maxStackSize(target.item);
            const uint32_t move = std::min(cap > target.count ? cap - target.count : 0, cursor.count);
            target.count += move;
            cursor.count -= move;
            if (cursor.count == 0) cursor = {};
        } else {
            std::swap(target, cursor);
        }
    } else if (cursor.empty()) {
        if (!target.empty()) {
            const uint32_t take = (target.count + 1) / 2;
            cursor = {target.item, take};
            target.count -= take;
            if (target.count == 0) target = {};
        }
    } else if (target.empty() || (target.item == cursor.item && target.count < maxStackSize(target.item))) {
        if (target.empty()) target = {cursor.item, 0};
        ++target.count;
        if (--cursor.count == 0) cursor = {};
    } else {
        std::swap(target, cursor);
    }
    player.setCursor(cursor);
}

void playerShiftClickIntoContainer(Container& container, Inventory& player, int slot) {
    if (slot < 0 || slot >= Inventory::kSlots) return;
    const ItemStack stack = player.slot(slot);
    if (stack.empty()) return;
    const ItemStack leftover = container.insert(stack);
    player.setSlot(slot, leftover);
}

// --- Bed -------------------------------------------------------------------------------------

Bed::Bed(DyeColor color, BedHalf half, Cell head, Cell foot)
    : color_(color), half_(half), head_(head), foot_(foot) {}

bool Bed::setCells(Cell head, Cell foot) {
    if (head == foot) return false;
    head_ = head;
    foot_ = foot;
    return true;
}

std::optional<Cell> Bed::otherHalf(const Cell& from) const {
    if (from == head_) return foot_;
    if (from == foot_) return head_;
    return std::nullopt;
}

// --- Banner ----------------------------------------------------------------------------------

bool Banner::addPattern(BannerPatternKind kind, DyeColor color) {
    if (pattern_count_ >= kMaxPatterns) return false;
    patterns_[static_cast<size_t>(pattern_count_)] = {kind, color};
    ++pattern_count_;
    return true;
}

void Banner::clearPatterns() {
    patterns_ = {};
    pattern_count_ = 0;
}

// --- Skull -----------------------------------------------------------------------------------

void Skull::setKind(SkullKind kind) {
    kind_ = kind;
    if (kind_ != SkullKind::Player) owner_.clear();
}

bool Skull::setOwner(std::string name) {
    if (kind_ != SkullKind::Player || name.empty()) return false;
    owner_ = std::move(name);
    return true;
}

// --- Piston ----------------------------------------------------------------------------------

PistonMove PistonRules::planMove(const BlockGrid& grid, const Cell& piston_cell, Direction direction, bool sticky,
                                 PistonAction action) {
    PistonMove plan;
    plan.action = action;
    const Cell step = directionOffset(direction);

    if (action == PistonAction::Retract) {
        plan.possible = true;
        if (!sticky) return plan;
        const Cell front = offsetCell(piston_cell, step);
        const BlockId id = grid.get(front);
        if (!isPassThrough(id) && !isImmovable(id)) plan.pulled = front;
        return plan;
    }

    Cell cursor = piston_cell;
    for (int i = 0; i < kMaxPush + 1; ++i) {
        cursor = offsetCell(cursor, step);
        const BlockId id = grid.get(cursor);
        if (isPassThrough(id)) {
            plan.possible = true; // room for the chain to slide into
            return plan;
        }
        if (isImmovable(id)) return plan; // blocked: possible stays false
        plan.moved.push_back(cursor);
    }
    // kMaxPush + 1 occupied cells: the chain is longer than a piston can move.
    return plan;
}

// --- BlockEntityRegistry ---------------------------------------------------------------------

BlockEntity* BlockEntityRegistry::create(const Cell& cell, BlockEntityKind kind) {
    if (entities_.count(cell) != 0) return nullptr;
    BlockEntity& entity = entities_[cell];
    entity.kind = kind;
    entity.payload = defaultPayload(kind);
    ++version_;
    return &entity;
}

bool BlockEntityRegistry::remove(const Cell& cell) {
    if (entities_.erase(cell) == 0) return false;
    ++version_;
    return true;
}

BlockEntity* BlockEntityRegistry::get(const Cell& cell) {
    const auto it = entities_.find(cell);
    return it != entities_.end() ? &it->second : nullptr;
}

const BlockEntity* BlockEntityRegistry::get(const Cell& cell) const {
    const auto it = entities_.find(cell);
    return it != entities_.end() ? &it->second : nullptr;
}

bool BlockEntityRegistry::contains(const Cell& cell) const { return entities_.count(cell) != 0; }

void BlockEntityRegistry::touch(const Cell& cell) {
    if (entities_.count(cell) == 0) return;
    ++version_;
}

void BlockEntityRegistry::clear() {
    if (entities_.empty()) return;
    entities_.clear();
    ++version_;
}

} // namespace mc
