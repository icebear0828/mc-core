#pragma once

// CS2 buy economy for the Elden Ring adapter: kill rewards, magazine prices and
// the pure purchase rule. No host access, so tests pin every number.

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace eldenring::cs {

inline constexpr uint32_t kCsStartingMoney = 800;
inline constexpr uint32_t kCsGunKillReward = 300;
// A knife kill is the anti-deadlock floor: when both ammo and money are gone,
// the knife can still earn enough for a magazine.
inline constexpr uint32_t kCsKnifeKillReward = 1500;

struct CsAmmoOffer {
    uint8_t slot;       // arsenal slot: 1 AK-47, 2 AWP, 3 M4A1-S
    uint8_t buy_key;    // number key in the buy panel (1, 2, 3)
    const char* weapon_id;
    const char* name;
    uint32_t price;
};

inline constexpr CsAmmoOffer kCsAmmoOffers[] = {
    {1, 1, "weapon_ak47", "AK-47", 200},
    {2, 2, "weapon_awp", "AWP", 250},
    {3, 3, "weapon_m4a1_silencer", "M4A1-S", 180},
};
inline constexpr size_t kCsAmmoOfferCount = sizeof(kCsAmmoOffers) / sizeof(kCsAmmoOffers[0]);

inline const CsAmmoOffer* csAmmoOfferForSlot(uint8_t slot) {
    for (const auto& offer : kCsAmmoOffers) {
        if (offer.slot == slot) return &offer;
    }
    return nullptr;
}

inline const CsAmmoOffer* csAmmoOfferForKey(uint8_t key) {
    for (const auto& offer : kCsAmmoOffers) {
        if (offer.buy_key == key) return &offer;
    }
    return nullptr;
}

// Arsenal slot 0 is the knife; every gun slot pays the gun reward.
inline uint32_t csKillReward(uint8_t weapon_slot) {
    return weapon_slot == 0 ? kCsKnifeKillReward : kCsGunKillReward;
}

struct CsPurchase {
    bool bought{false};
    uint32_t reserve{0};
    uint32_t spent{0};
};

// Buys one magazine into the reserve, capped at the weapon's reserve capacity.
inline CsPurchase csBuyMagazine(uint32_t money, uint32_t reserve, uint32_t reserve_cap,
                                uint32_t magazine_capacity, uint32_t price) {
    if (reserve >= reserve_cap || money < price) return {false, reserve, 0};
    const uint32_t add = std::min(magazine_capacity, reserve_cap - reserve);
    return {true, reserve + add, price};
}

} // namespace eldenring::cs
