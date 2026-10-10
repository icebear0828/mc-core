#pragma once

// Pure helpers for drawing the real Minecraft HUD sprites in the Elden Ring overlay: which atlas cell an item uses, and
// the nearest-neighbour enlargement that keeps the sprites crisp even though ImGui's DX12 backend samples with a linear filter.

#include "mc/hud_atlas.hpp"
#include "mc/types.hpp"

#include <cstdint>
#include <vector>

namespace eldenring::render {

// The atlas cell of an item's icon, or nullptr when the atlas has none (only None).
inline const mc::hud::HudUV* uvForItem(mc::ItemId item) {
    using mc::ItemId;
    switch (item) {
        case ItemId::DiamondSword: return &mc::hud::kUV_ITEM_DIAMOND_SWORD;
        case ItemId::DiamondPickaxe: return &mc::hud::kUV_ITEM_DIAMOND_PICKAXE;
        case ItemId::BlockDirt: return &mc::hud::kUV_ITEM_DIRT;
        case ItemId::BlockStone: return &mc::hud::kUV_ITEM_STONE;
        case ItemId::BlockTnt: return &mc::hud::kUV_ITEM_TNT;
        case ItemId::GoldenApple: return &mc::hud::kUV_ITEM_GOLDEN_APPLE;
        case ItemId::Bow: return &mc::hud::kUV_ITEM_BOW;
        case ItemId::Elytra: return &mc::hud::kUV_ITEM_ELYTRA;
        case ItemId::TotemOfUndying: return &mc::hud::kUV_ITEM_TOTEM_OF_UNDYING;
        case ItemId::Arrow: return &mc::hud::kUV_ITEM_ARROW;
        case ItemId::Trident: return &mc::hud::kUV_ITEM_TRIDENT;
        case ItemId::FlintAndSteel: return &mc::hud::kUV_ITEM_FLINT_AND_STEEL;
        case ItemId::EnderPearl: return &mc::hud::kUV_ITEM_ENDER_PEARL;
        case ItemId::EnchantedGoldenApple: return &mc::hud::kUV_ITEM_ENCHANTED_GOLDEN_APPLE;
        case ItemId::Bread: return &mc::hud::kUV_ITEM_BREAD;
        case ItemId::CookedBeef: return &mc::hud::kUV_ITEM_COOKED_BEEF;
        case ItemId::FireworkRocket: return &mc::hud::kUV_ITEM_FIREWORK_ROCKET;
        case ItemId::ZombieSpawnEgg: return &mc::hud::kUV_ITEM_ZOMBIE_SPAWN_EGG;
        case ItemId::BowPulling0: return &mc::hud::kUV_ITEM_BOW_PULLING_0;
        case ItemId::BowPulling1: return &mc::hud::kUV_ITEM_BOW_PULLING_1;
        case ItemId::BowPulling2: return &mc::hud::kUV_ITEM_BOW_PULLING_2;
        default: return nullptr;
    }
}

// Each source pixel becomes a scale x scale block. RGBA8 in and out.
inline std::vector<uint8_t> upscaleNearest(const uint8_t* rgba, unsigned w, unsigned h, unsigned scale) {
    std::vector<uint8_t> out;
    if (!rgba || w == 0 || h == 0 || scale == 0) return out;
    const size_t ow = static_cast<size_t>(w) * scale;
    out.resize(ow * h * scale * 4);
    for (unsigned y = 0; y < h * scale; ++y) {
        const uint8_t* src_row = rgba + static_cast<size_t>(y / scale) * w * 4;
        uint8_t* dst_row = out.data() + static_cast<size_t>(y) * ow * 4;
        for (size_t x = 0; x < ow; ++x) {
            const uint8_t* s = src_row + (x / scale) * 4;
            uint8_t* d = dst_row + x * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
    return out;
}

} // namespace eldenring::render
