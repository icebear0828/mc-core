#include "mc/hand_items.hpp"

#include "mc/blocks.hpp"
#include "mc/bow.hpp"
#include "mc/hunger.hpp"

#include <algorithm>

namespace mc {

namespace {

bool isFood(ItemId item) { return foodValue(item).nutrition > 0; }

bool isProp3d(ItemId item) { return item == ItemId::Trident; }

// The main hand owns the button when it holds an item that claims it. A present item that does
// not claim the button (a sword, a pickaxe) lets the offhand act, as in Minecraft.
bool offhandMayAct(ItemId main_item) { return main_item == ItemId::None || !claimsRightClick(main_item); }

std::optional<Hand> pickItem(ItemId wanted, ItemId main_item, ItemId off_item) {
    if (main_item == wanted) return Hand::Main;
    if (off_item == wanted && offhandMayAct(main_item)) return Hand::Off;
    return std::nullopt;
}

std::optional<Hand> pickFood(ItemId main_item, ItemId off_item) {
    if (isFood(main_item)) return Hand::Main;
    if (isFood(off_item) && offhandMayAct(main_item)) return Hand::Off;
    return std::nullopt;
}

void finish(HandItems& out, const HandItemInput& in) {
    for (Hand h : {Hand::Main, Hand::Off}) {
        HandItem& item = out[h];
        item.present = item.item != ItemId::None;
        item.model = !item.present ? HandModel::Sprite
                     : isProp3d(item.item) ? HandModel::Trident
                     : item.item == ItemId::Shield ? HandModel::Shield
                     : blockForItem(item.item) != BlockId::Air ? HandModel::Block
                     : HandModel::Sprite;
        const float bow_power = item.use == HandUse::BowDraw ? in.bow_charge : 0.f;
        item.shown = item.present ? shownBow(item.item, bow_power) : ItemId::None;
    }
}

} // namespace

bool claimsRightClick(ItemId item) {
    switch (item) {
        case ItemId::Bow:
        case ItemId::Trident:
        case ItemId::Shield:
        case ItemId::GoldenApple:
        case ItemId::EnchantedGoldenApple:
        case ItemId::Bread:
        case ItemId::CookedBeef:
        case ItemId::ZombieSpawnEgg:
        case ItemId::CreeperSpawnEgg:
        case ItemId::SkeletonSpawnEgg:
        case ItemId::FlintAndSteel:
        case ItemId::EnderPearl:
        case ItemId::FireworkRocket: return true;
        default: return blockForItem(item) != BlockId::Air; // every placeable block
    }
}

std::optional<Hand> handForUse(ItemId wanted, ItemId main_item, ItemId off_item) {
    if (wanted == ItemId::None) return std::nullopt;
    if (isFood(wanted)) return pickFood(main_item, off_item);
    return pickItem(wanted, main_item, off_item);
}

HandItems planHandItems(const HandItemInput& in) {
    HandItems out;
    out.main.item = in.main_item;
    out.off.item = in.off_item;
    out.main.present = in.main_item != ItemId::None;
    out.off.present = in.off_item != ItemId::None;

    // One right-click action per frame; the same precedence the animator uses.
    if (in.trident_active) {
        if (const auto hand = pickItem(ItemId::Trident, in.main_item, in.off_item)) {
            HandItem& item = out[*hand];
            item.use = in.riptide ? HandUse::Riptide : HandUse::TridentCharge;
            item.progress = in.riptide ? 0.f : std::clamp(in.trident_charge, 0.f, 1.f);
        }
    } else if (in.bow_active) {
        if (const auto hand = pickItem(ItemId::Bow, in.main_item, in.off_item)) {
            HandItem& item = out[*hand];
            item.use = HandUse::BowDraw;
            item.progress = std::clamp(in.bow_charge, 0.f, 1.f);
        }
    } else if (in.blocking) {
        if (const auto hand = pickItem(ItemId::Shield, in.main_item, in.off_item)) {
            HandItem& item = out[*hand];
            item.use = HandUse::Block;
            item.raised = true;
        }
    } else if (in.eating) {
        if (const auto hand = pickFood(in.main_item, in.off_item)) {
            HandItem& item = out[*hand];
            item.use = HandUse::Eat;
            item.progress = std::clamp(in.eat_progress, 0.f, 1.f);
        }
    }

    if (in.swing > 0.f && out.main.use == HandUse::None) {
        out.main.use = HandUse::Swing;
        out.main.progress = std::clamp(in.swing, 0.f, 1.f);
    }

    finish(out, in);
    return out;
}

} // namespace mc
