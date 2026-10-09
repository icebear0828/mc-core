#pragma once

// The player's native model parts (hiding them while the Steve rig is drawn). PlayerIns+0x648 -> CSChrAsmModelIns;
// [asm+0x28] holds 27 part pointers (0xD8 bytes), 14 or 15 of them in use; part+0x18 -> CSModelDispEntity whose
// disp_flags1 at +0x20 has bit 0 set while the part is drawn (value 0x000100A1 live). Slot-to-part names are NOT
// known, so every non-null slot is treated alike. Reading only; the loader does the (guarded) write.

#include "eldenring_rtti.hpp"

#include <vector>

namespace eldenring::live {

namespace layout {
inline constexpr uintptr_t kAsmModelInPlayerIns = 0x648;
inline constexpr uint32_t kAsmModelVtableRva = 0x2B35980;
inline constexpr const char* kAsmModelRtti = ".?AVCSChrAsmModelIns@CS@@";
inline constexpr uintptr_t kAsmPartPointers = 0x28;
inline constexpr unsigned kAsmPartSlots = 27;
inline constexpr uintptr_t kPartDispEntity = 0x18;
inline constexpr uintptr_t kDispFlags1 = 0x20;
inline constexpr uint32_t kDispVisibleBit = 1u;
inline constexpr uintptr_t kDispFlags2 = 0x24;
} // namespace layout

// Clears `mask` bits of `flags`; restoring puts back only the masked bits from `original` and keeps the rest of the
// current value (the game may have changed other bits meanwhile).
inline uint32_t hideBits(uint32_t flags, uint32_t mask) { return flags & ~mask; }
inline uint32_t restoreBits(uint32_t flags, uint32_t original, uint32_t mask) { return (flags & ~mask) | (original & mask); }

// Addresses of the disp_flags1 words of every part currently attached to the player. Empty when the model object is
// missing or not a CSChrAsmModelIns.
inline std::vector<uintptr_t> collectDispFlagAddresses(const IMemoryReader& reader, uintptr_t image_base, uintptr_t player_chr) {
    std::vector<uintptr_t> out;
    uint64_t model = 0;
    if (player_chr == 0 || !reader.read(player_chr + layout::kAsmModelInPlayerIns, &model, sizeof(model)) || model == 0) return out;
    const auto m = static_cast<uintptr_t>(model);
    if (!objectIsClass(reader, image_base, m, layout::kAsmModelVtableRva, layout::kAsmModelRtti)) return out;
    for (unsigned i = 0; i < layout::kAsmPartSlots; ++i) {
        uint64_t part = 0, disp = 0;
        uint32_t flags = 0;
        if (!reader.read(m + layout::kAsmPartPointers + i * sizeof(uint64_t), &part, sizeof(part)) || part == 0) continue;
        if (!reader.read(static_cast<uintptr_t>(part) + layout::kPartDispEntity, &disp, sizeof(disp)) || disp == 0) continue;
        const uintptr_t at = static_cast<uintptr_t>(disp) + layout::kDispFlags1;
        if (!reader.read(at, &flags, sizeof(flags))) continue;
        out.push_back(at);
    }
    return out;
}

} // namespace eldenring::live
