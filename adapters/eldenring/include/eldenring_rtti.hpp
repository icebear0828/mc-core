#pragma once

// Class identification through MSVC x64 RTTI. Fixed vtable RVAs are measured per build and the same C++ class
// can appear under a base or a derived vtable (CSChrDamageModule / CSEnemyDamageModule / CSPlayerDamageModule),
// so instances are identified by the type name found through the vtable's Complete Object Locator instead:
//   vtable[-1] -> COL; COL+0x0C = RVA of the TypeDescriptor; TypeDescriptor+0x10 = ".?AVName@Ns@@".

#include "eldenring_live.hpp"
#include "eldenring_perfstats.hpp"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace eldenring::live {

namespace rtti {
inline constexpr uintptr_t kTypeDescriptorRvaInCol = 0x0C;
inline constexpr uintptr_t kNameInTypeDescriptor = 0x10;
inline constexpr uint32_t kColSignatureX64 = 1;
inline constexpr size_t kMaxNameLength = 160;

// Direct-mapped, per-thread cache of vtable -> type name. A class's vtable always yields the same
// name for the life of the process, so a hit skips the COL walk and the byte-by-byte name read.
// Only successful reads are cached: a transient read failure is retried next time.
inline constexpr size_t kNameCacheEntries = 256;
struct NameCacheEntry {
    uint64_t vtable{0};
    uintptr_t image_base{0};
    uint32_t length{0};
    bool valid{false};
    char name[kMaxNameLength]{};
};

inline NameCacheEntry* nameCache() {
    static thread_local NameCacheEntry cache[kNameCacheEntries];
    return cache;
}

inline void resetNameCacheForTesting() {
    NameCacheEntry* cache = nameCache();
    for (size_t i = 0; i < kNameCacheEntries; ++i) cache[i] = NameCacheEntry{};
}
} // namespace rtti

// Returns a view into the per-thread cache, valid until the next call on the same thread.
inline std::optional<std::string_view> readTypeNameViewOfVtable(const IMemoryReader& reader, uintptr_t image_base,
                                                               uint64_t vtable, size_t image_size = 0x10000000) {
    if (vtable < sizeof(uint64_t)) return std::nullopt;
    if (image_base != 0 && (vtable < image_base || vtable >= image_base + image_size)) return std::nullopt;

    const size_t slot = static_cast<size_t>((vtable >> 4) & (rtti::kNameCacheEntries - 1));
    rtti::NameCacheEntry& entry = rtti::nameCache()[slot];
    if (entry.valid && entry.vtable == vtable && entry.image_base == image_base) {
        perfstats::noteRttiCacheHit();
        return std::string_view(entry.name, entry.length);
    }

    perfstats::noteSlowRtti();
    uint64_t col = 0;
    if (!reader.read(static_cast<uintptr_t>(vtable) - sizeof(uint64_t), &col, sizeof(col)) || col == 0) return std::nullopt;
    if (image_base != 0 && (col < image_base || col >= image_base + image_size)) return std::nullopt;
    uint32_t signature = 0, td_rva = 0;
    if (!reader.read(static_cast<uintptr_t>(col), &signature, sizeof(signature)) || signature != rtti::kColSignatureX64) {
        return std::nullopt;
    }
    if (!reader.read(static_cast<uintptr_t>(col) + rtti::kTypeDescriptorRvaInCol, &td_rva, sizeof(td_rva)) || td_rva == 0 ||
        (image_base != 0 && td_rva >= image_size)) {
        return std::nullopt;
    }
    char name[rtti::kMaxNameLength] = {};
    const uintptr_t name_at = image_base + td_rva + rtti::kNameInTypeDescriptor;
    uint32_t length = 0;
    for (; length < rtti::kMaxNameLength; ++length) {
        char c = 0;
        if (!reader.read(name_at + length, &c, 1)) return std::nullopt;
        if (c == 0) break;
        name[length] = c;
    }
    if (length == 0 || length == rtti::kMaxNameLength) return std::nullopt; // empty or unterminated

    std::memcpy(entry.name, name, length);
    entry.vtable = vtable;
    entry.image_base = image_base;
    entry.length = length;
    entry.valid = true;
    return std::string_view(entry.name, entry.length);
}

// The mangled type name behind a vtable address, or nullopt for anything that does not look like an x64 COL.
inline std::optional<std::string> readTypeNameOfVtable(const IMemoryReader& reader, uintptr_t image_base, uint64_t vtable, size_t image_size = 0x10000000) {
    const auto view = readTypeNameViewOfVtable(reader, image_base, vtable, image_size);
    if (!view) return std::nullopt;
    return std::string(*view);
}

// True when `object`'s vtable is the measured one OR its RTTI name contains `name_token` (for example
// "CSChrPhysicsModule"). Either is enough: the RVA works on the measured build, the name survives patches.
inline bool objectIsClass(const IMemoryReader& reader, uintptr_t image_base, uintptr_t object, uint32_t measured_vtable_rva,
                          const char* name_token) {
    uint64_t vtable = 0;
    if (object == 0 || !reader.read(object, &vtable, sizeof(vtable))) return false;
    if (measured_vtable_rva != 0 && vtable == image_base + measured_vtable_rva) return true;
    const auto name = readTypeNameViewOfVtable(reader, image_base, vtable);
    return name && name->find(name_token) != std::string_view::npos;
}

} // namespace eldenring::live
