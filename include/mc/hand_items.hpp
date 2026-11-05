#pragma once

// What each hand holds and what it is doing, resolved once per frame.
//
// First person, third person, the animator and the interaction handler all used to decide this
// separately (and only for the item types they knew about). This module is the single source of
// truth for the per-hand use action and the presentation model. The hand that owns a right click
// is resolved by mc::resolveUse (item_use.hpp); HandItemInput carries that hand so presentation
// follows the real use. Presentation code consumes a HandItems and never re-derives priorities.
// Pure logic, unit-tested.

#include "mc/types.hpp"

#include <optional>

namespace mc {

enum class Hand {
    Main,
    Off
};

enum class HandUse {
    None,
    Swing,          // attack swing (main hand)
    Eat,            // food being eaten
    BowDraw,        // bow being drawn
    Block,          // shield raised
    TridentCharge,  // trident charge (two-handed; the other arm braces)
    Riptide         // riptide spin (two-handed; the pose itself is global)
};

// How the held item is drawn. One classification shared by first person and third person, so a
// new item only has to be added here instead of in every renderer's item switch.
enum class HandModel {
    Sprite,  // flat extruded sprite (the atlas cell)
    Block,   // textured cube
    Shield,  // ShieldModel plate + handle
    Trident  // Bedrock TridentModel prop
};

struct HandItem {
    Hand hand{Hand::Main};
    ItemId item{ItemId::None};
    bool present{false};
    HandUse use{HandUse::None};
    float progress{0.f};            // Eat/BowDraw/TridentCharge 0..1; Swing uses the swing progress
    bool raised{false};             // a shield held up (HandUse::Block)
    ItemId shown{ItemId::None};     // the sprite actually drawn (the bow's pulling stage)
    HandModel model{HandModel::Sprite};
};

struct HandItems {
    HandItem main{Hand::Main};
    HandItem off{Hand::Off};

    [[nodiscard]] const HandItem& operator[](Hand h) const { return h == Hand::Main ? main : off; }
    [[nodiscard]] HandItem& operator[](Hand h) { return h == Hand::Main ? main : off; }
    [[nodiscard]] const HandItem& other(Hand h) const { return (*this)[h == Hand::Main ? Hand::Off : Hand::Main]; }

    [[nodiscard]] const HandItem* findUse(HandUse u) const {
        if (main.use == u) return &main;
        if (off.use == u) return &off;
        return nullptr;
    }
    [[nodiscard]] bool anyUse(HandUse u) const { return findUse(u) != nullptr; }

    // The hand holding `wanted`, or nullptr. Callers must check before reading the hand.
    [[nodiscard]] const HandItem* findItem(ItemId wanted) const {
        if (main.present && main.item == wanted) return &main;
        if (off.present && off.item == wanted) return &off;
        return nullptr;
    }
};

struct HandItemInput {
    ItemId main_item{ItemId::None};
    ItemId off_item{ItemId::None};
    float swing{0.f};          // main-hand attack swing progress 0..1
    bool bow_active{false};    // a bow is being drawn (the caller already gated usability)
    float bow_charge{0.f};     // 0..1 draw power
    bool eating{false};        // food is being eaten
    float eat_progress{0.f};   // 0..1
    bool trident_active{false};// the trident is charging or riptiding
    float trident_charge{0.f}; // 0..1 charge
    bool riptide{false};       // the active trident is in its riptide spin
    bool blocking{false};      // a shield is raised
    // The hand that actually owns the use (mc::resolveUse). When set, it wins over the fallback
    // main-hand-first classification, so presentation follows the real use: a block in the main
    // hand no longer hides an offhand bow that is actually drawing because the block could not be
    // placed.
    std::optional<Hand> bow_hand;
    std::optional<Hand> eat_hand;
    std::optional<Hand> shield_hand;
    std::optional<Hand> trident_hand;
    std::optional<Hand> swing_hand; // the arm that used the item; unset = the main hand
};

// Whether the item has a right-click use of its own (the table is mc::useSpec, item_use.hpp).
// This is a static classification; which hand actually acts is decided per click by
// mc::resolveUse, because a main-hand use that cannot start (a block aimed at the sky, a bow with
// no arrows) lets the offhand try.
[[nodiscard]] bool claimsRightClick(ItemId item);

// The presentation fallback: which hand should perform the use of `wanted` when the caller has
// no explicit hand from mc::resolveUse. `std::nullopt` when neither hand holds it, or the offhand
// holds it while the main hand claims the button. The interaction handler no longer uses this to
// decide the click; it passes the resolved hand through HandItemInput.
[[nodiscard]] std::optional<Hand> handForUse(ItemId wanted, ItemId main_item, ItemId off_item);

// Resolve both hands for this frame. At most one right-click action is active; the order matches
// the animator's (trident > bow > shield > eat). A swing is assigned to the main hand when no
// other use is active there.
[[nodiscard]] HandItems planHandItems(const HandItemInput& input);

} // namespace mc
