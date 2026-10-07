#pragma once

// Binding to the live Elden Ring process (eldenring.exe, Steam, offline without EAC). Platform independent:
// memory is accessed through IMemoryReader, so the logic is unit-tested against fake memory on any host.
//
// Evidence levels are stated per constant. Only read what is justified; nothing here writes to the game.

#include <cstddef>
#include <cstdint>

namespace eldenring::live {

namespace layout {

// WorldChrManImp singleton global (`mov rcx,[0x143d69ff8]` at 0x1403cf09b). A fixed RVA of one build, found
// by static analysis only: replace with a signature before shipping. 0 while on the title screen.
inline constexpr uint32_t kWorldChrManGlobalRva = 0x3D69FF8;
// Main player: [WorldChrMan+0x1E508] (static analysis; not yet read in the live game).
inline constexpr uintptr_t kPlayerInsInWorldChrMan = 0x1E508;

// ChrIns+0x190 points at the module container: a table of 64 module pointers (registerModule bounds-checks the
// index against 0x40). Slot 0 is the CSChrDataModule.
inline constexpr uintptr_t kModuleContainerInChrIns = 0x190;
inline constexpr uintptr_t kChrDataModuleSlot = 0;
inline constexpr uint32_t kChrDataModuleVtableRva = 0x2A380B8; // .?AVCSChrDataModule@CS@@ (class check)
// CSChrDataModule::getOwnerChr is `mov rax,[rcx+8]; ret` (disassembly), so the module names its character.
inline constexpr uintptr_t kOwnerInDataModule = 0x8;

// Vitals, measured live by reading the module while putting the Crimson Amber Medallion on and off:
//   +0x138 hp stayed 522, +0x13C and +0x140 went 522 -> 553 -> 522, +0x144 stayed 522 (base max).
// +0x13C is the effective maximum (it is also what the hp clamp reads). Hurt/healed samples (553 -> 455 -> 250 ->
// 500 -> 402) moved only +0x138; +0x13C, +0x140 and +0x144 stayed put.
inline constexpr uintptr_t kDataHp = 0x138;
inline constexpr uintptr_t kDataMaxHp = 0x13C;
inline constexpr uintptr_t kDataBaseMaxHp = 0x144;
inline constexpr int32_t kMaxPlausibleMaxHp = 100000;

} // namespace layout

class IMemoryReader {
public:
    virtual ~IMemoryReader() = default;
    // Must fail (return false) instead of faulting on unreadable memory.
    virtual bool read(uintptr_t address, void* out, size_t size) const = 0;
};

struct Vitals {
    int32_t hp{0};
    int32_t max_hp{0};
};

// Reads a character's hp from its CSChrDataModule. Only accepts a module whose vtable is the data module's AND
// whose owner back-pointer is this character; leaves `out` untouched and returns false for anything else
// (null pointers, unreadable memory, wrong class, foreign owner, implausible numbers).
inline bool readVitals(const IMemoryReader& reader, uintptr_t image_base, uintptr_t chr, Vitals& out) {
    if (chr == 0) return false;
    uint64_t container = 0, module = 0, vtable = 0, owner = 0;
    if (!reader.read(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0) return false;
    if (!reader.read(static_cast<uintptr_t>(container) + layout::kChrDataModuleSlot * sizeof(uint64_t), &module,
                     sizeof(module)) ||
        module == 0) {
        return false;
    }
    const auto m = static_cast<uintptr_t>(module);
    if (!reader.read(m, &vtable, sizeof(vtable)) || vtable != image_base + layout::kChrDataModuleVtableRva) return false;
    if (!reader.read(m + layout::kOwnerInDataModule, &owner, sizeof(owner)) || owner != chr) return false;
    int32_t hp = 0, max_hp = 0;
    if (!reader.read(m + layout::kDataHp, &hp, sizeof(hp)) || !reader.read(m + layout::kDataMaxHp, &max_hp, sizeof(max_hp))) {
        return false;
    }
    if (max_hp <= 0 || max_hp > layout::kMaxPlausibleMaxHp || hp < 0 || hp > max_hp) return false;
    out.hp = hp;
    out.max_hp = max_hp;
    return true;
}

// The local player's vitals through the WorldChrMan global. False while not in a world (title screen, loading).
inline bool readPlayerVitals(const IMemoryReader& reader, uintptr_t image_base, Vitals& out) {
    uint64_t world = 0, player = 0;
    if (!reader.read(image_base + layout::kWorldChrManGlobalRva, &world, sizeof(world)) || world == 0) return false;
    if (!reader.read(static_cast<uintptr_t>(world) + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0) {
        return false;
    }
    return readVitals(reader, image_base, static_cast<uintptr_t>(player), out);
}

} // namespace eldenring::live
