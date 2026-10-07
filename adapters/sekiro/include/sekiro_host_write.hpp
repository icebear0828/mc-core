#pragma once

// The only places the adapter writes into the running game for combat. Each write is a single int32 (the
// current hp), preceded by class checks of every object on the chain and plausibility checks of the values,
// and verified by reading back. Enemy health is only ever lowered, the player's only ever raised, so a bug
// upstream cannot heal an enemy or hurt the player through here.

#include "sekiro_model.hpp"

#include <cstdint>

namespace sekiro::live {

class HostHealthWriter {
public:
    enum class Result {
        Written,
        Unchanged,   // the write would not move health in the allowed direction
        Rejected,    // an object failed validation; nothing was written
        WriteFailed, // the write (or its read-back) failed
    };

    HostHealthWriter(const IMemoryReader& reader, IMemoryWriter& writer, uintptr_t image_base)
        : reader_(reader), writer_(writer), base_(image_base) {}

    // EnemyIns -> [+0x10b8] -> [+0x1f8] SprjChrDataModule: hp at +0x130, max at +0x160.
    Result lowerEnemyHealth(uintptr_t enemy, float new_health);
    // PlayerIns -> [+0x10b8] -> [+0x1e8] SprjChrDataModule: hp at +0x130, max at +0x138.
    Result raisePlayerHealth(uintptr_t player, float new_health);

private:
    Result apply(uintptr_t character, uint32_t character_vtable_rva, uintptr_t module_offset, uintptr_t max_hp_offset,
                 float new_health, bool lowering);

    const IMemoryReader& reader_;
    IMemoryWriter& writer_;
    uintptr_t base_;
};

} // namespace sekiro::live
