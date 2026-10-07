#pragma once

// Read-only probe of the game's own damage pipeline (Sekiro 1.6.0.0). The user located
//   DealDamage(SprjEnemyDamageModule* target, ChrIns* attacker, DamageData* data, uint8 flag)  at RVA 0xB68FF0
// whose DamageData (0x200 bytes) carries hp damage at +0x24, posture damage at +0x28/+0x1E0 and the stagger
// level at +0x54. Calling that function ourselves would give native hit reactions, but DamageData probably holds
// pointers to transient attack records, so the layout is observed first: the probe passes every call through
// unchanged and logs the arguments and a dump of the data.

#include "sekiro_live.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace sekiro::live {

inline constexpr uint32_t kDealDamageRva = 0xB68FF0;
inline constexpr uint8_t kDealDamagePrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89};
inline constexpr size_t kDamageDataSize = 0x200;

// True when the bytes at image_base+rva are exactly `expected`: the build we analysed. Anything else (another
// game version, a packer still decrypting) must not be hooked.
template <size_t N>
bool prologueMatches(const IMemoryReader& reader, uintptr_t image_base, uint32_t rva, const uint8_t (&expected)[N]) {
    uint8_t actual[N]{};
    if (!reader.read(image_base + rva, actual, N)) return false;
    for (size_t i = 0; i < N; ++i) {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

// One line per known field and per non-zero 8-byte word, pointers classified (image / readable heap / dangling).
std::string describeDamageData(const uint8_t* data, size_t size, const IMemoryReader& reader, uintptr_t image_base, size_t image_size);

} // namespace sekiro::live
