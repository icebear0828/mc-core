#include "sekiro_host_write.hpp"

#include <algorithm>
#include <cmath>

namespace sekiro::live {

namespace {
constexpr uint32_t kPlayerInsVtableRva = 0x2A2B338;
} // namespace

HostHealthWriter::Result HostHealthWriter::lowerEnemyHealth(uintptr_t enemy, float new_health) {
    return apply(enemy, layout::kEnemyInsVtableRva, layout::kEnemyDataModuleInContainer, layout::kEnemyMaxHp, new_health, true);
}

HostHealthWriter::Result HostHealthWriter::raisePlayerHealth(uintptr_t player, float new_health) {
    return apply(player, kPlayerInsVtableRva, layout::kChrDataModuleInContainer, layout::kDataMaxHp, new_health, false);
}

HostHealthWriter::Result HostHealthWriter::apply(uintptr_t character, uint32_t character_vtable_rva, uintptr_t module_offset,
                                                 uintptr_t max_hp_offset, float new_health, bool lowering) {
    if (character == 0 || !std::isfinite(new_health)) return Result::Rejected;

    auto readPtr = [&](uintptr_t address, uintptr_t& out) {
        uint64_t v = 0;
        if (!reader_.read(address, &v, sizeof(v))) return false;
        out = static_cast<uintptr_t>(v);
        return true;
    };

    uintptr_t vtable = 0, container = 0, module = 0, module_vtable = 0;
    if (!readPtr(character, vtable) || vtable != base_ + character_vtable_rva) return Result::Rejected;
    if (!readPtr(character + layout::kModuleContainerInChrIns, container) || container == 0) return Result::Rejected;
    if (!readPtr(container + module_offset, module) || module == 0) return Result::Rejected;
    if (!readPtr(module, module_vtable) || module_vtable != base_ + layout::kChrDataModuleVtableRva) return Result::Rejected;

    int32_t hp = 0, max_hp = 0;
    if (!reader_.read(module + layout::kDataHp, &hp, sizeof(hp)) || !reader_.read(module + max_hp_offset, &max_hp, sizeof(max_hp))) {
        return Result::Rejected;
    }
    if (max_hp <= 0 || max_hp > layout::kMaxPlausibleMaxHp || hp < 0 || hp > max_hp) return Result::Rejected;

    const int32_t target = static_cast<int32_t>(std::lround(std::clamp(new_health, 0.0f, static_cast<float>(max_hp))));
    if (lowering ? target >= hp : target <= hp) return Result::Unchanged;

    if (!writer_.write(module + layout::kDataHp, &target, sizeof(target))) return Result::WriteFailed;
    int32_t check = 0;
    if (!reader_.read(module + layout::kDataHp, &check, sizeof(check)) || check != target) return Result::WriteFailed;
    return Result::Written;
}

} // namespace sekiro::live
