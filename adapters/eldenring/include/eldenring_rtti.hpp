#pragma once

// Class identification through MSVC x64 RTTI. Fixed vtable RVAs are measured per build and the same C++ class
// can appear under a base or a derived vtable (CSChrDamageModule / CSEnemyDamageModule / CSPlayerDamageModule),
// so instances are identified by the type name found through the vtable's Complete Object Locator instead:
//   vtable[-1] -> COL; COL+0x0C = RVA of the TypeDescriptor; TypeDescriptor+0x10 = ".?AVName@Ns@@".

#include "eldenring_live.hpp"

#include <optional>
#include <string>

namespace eldenring::live {

namespace rtti {
inline constexpr uintptr_t kTypeDescriptorRvaInCol = 0x0C;
inline constexpr uintptr_t kNameInTypeDescriptor = 0x10;
inline constexpr uint32_t kColSignatureX64 = 1;
inline constexpr size_t kMaxNameLength = 160;
} // namespace rtti

// The mangled type name behind a vtable address, or nullopt for anything that does not look like an x64 COL.
inline std::optional<std::string> readTypeNameOfVtable(const IMemoryReader& reader, uintptr_t image_base, uint64_t vtable) {
    if (vtable < sizeof(uint64_t)) return std::nullopt;
    uint64_t col = 0;
    if (!reader.read(static_cast<uintptr_t>(vtable) - sizeof(uint64_t), &col, sizeof(col)) || col == 0) return std::nullopt;
    uint32_t signature = 0, td_rva = 0;
    if (!reader.read(static_cast<uintptr_t>(col), &signature, sizeof(signature)) || signature != rtti::kColSignatureX64) {
        return std::nullopt;
    }
    if (!reader.read(static_cast<uintptr_t>(col) + rtti::kTypeDescriptorRvaInCol, &td_rva, sizeof(td_rva)) || td_rva == 0) {
        return std::nullopt;
    }
    std::string name;
    const uintptr_t name_at = image_base + td_rva + rtti::kNameInTypeDescriptor;
    for (size_t i = 0; i < rtti::kMaxNameLength; ++i) {
        char c = 0;
        if (!reader.read(name_at + i, &c, 1)) return std::nullopt;
        if (c == 0) return name.empty() ? std::nullopt : std::optional<std::string>(name);
        name.push_back(c);
    }
    return std::nullopt; // unterminated
}

// True when `object`'s vtable is the measured one OR its RTTI name contains `name_token` (for example
// "CSChrPhysicsModule"). Either is enough: the RVA works on the measured build, the name survives patches.
inline bool objectIsClass(const IMemoryReader& reader, uintptr_t image_base, uintptr_t object, uint32_t measured_vtable_rva,
                          const char* name_token) {
    uint64_t vtable = 0;
    if (object == 0 || !reader.read(object, &vtable, sizeof(vtable))) return false;
    if (measured_vtable_rva != 0 && vtable == image_base + measured_vtable_rva) return true;
    const auto name = readTypeNameOfVtable(reader, image_base, vtable);
    return name && name->find(name_token) != std::string::npos;
}

} // namespace eldenring::live
