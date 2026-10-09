#pragma once

// Locating the game's global singleton variables by signature instead of by a fixed RVA. A signature finds an
// instruction that stores/loads a RIP-relative global; the global's address is then computed from its disp32.
// Uniqueness alone is not correctness: after a patch a short signature can match exactly once at the WRONG
// place, so a resolved global is only accepted when the object it points to has the expected RTTI type name.

#include "eldenring_rtti.hpp"
#include "eldenring_sigscan.hpp"

#include <cstring>
#include <optional>

namespace eldenring::live {

struct SingletonSig {
    const char* name;           // for diagnostics
    const char* pattern;        // ?? marks the disp32
    uint32_t insn_offset;       // from the match start to the instruction that holds the disp32
    uint32_t insn_length;       // that instruction's length
    uint32_t disp_offset;       // from the match start to the disp32
    const char* rtti_token;     // the singleton's full mangled type name (read from the game's COL)
};

namespace sigs {
// docs/ELDENRING_REVERSE.md 7.3 (2.7.1.0; uniqueness verified by the reverser on the 87 MB image, not by us).
inline constexpr SingletonSig kCSNowLoadingHelper = {
    "CSNowLoadingHelper",
    "90 48 89 05 ?? ?? ?? ?? 48 83 3D ?? ?? ?? ?? 00 75 ?? 4C 8B 05 ?? ?? ?? ?? 4C 89 45 10 BA 08 00 00 00 B9 A0 00 00 00",
    1, 7, 4, ".?AVCSNowLoadingHelperImp@CS@@"};
inline constexpr SingletonSig kCSMenuMan = {"CSMenuMan", "90 48 89 05 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 85 C0", 1, 7, 4,
                                            ".?AVCSMenuManImp@CS@@"};
inline constexpr SingletonSig kCSFade = {"CSFade", "48 8B F8 48 89 3D ?? ?? ?? ?? 48 8B C6", 3, 7, 6, ".?AVCSFadeImp@CS@@"};
inline constexpr SingletonSig kWorldChrMan = {
    "WorldChrMan",
    "41 FF 10 4C 8B 03 48 8B D6 48 8B CB 41 FF 50 68 48 89 3D ?? ?? ?? ?? 48 8B 35 ?? ?? ?? ??", 16, 7, 19, ".?AVWorldChrManImp@CS@@"};
} // namespace sigs

// RVA of the global variable (image mapped at its RVAs), or nullopt unless the signature matches exactly once and
// the disp32 stays inside the image.
inline std::optional<uint32_t> locateGlobalRva(const uint8_t* image, size_t size, const SingletonSig& sig) {
    const auto parsed = Signature::parse(sig.pattern);
    if (!parsed) return std::nullopt;
    const ScanResult r = scanUnique(image, size, *parsed);
    if (r.status != ScanStatus::Unique) return std::nullopt;
    if (r.offset + sig.disp_offset + sizeof(int32_t) > size) return std::nullopt;
    int32_t disp = 0;
    std::memcpy(&disp, image + r.offset + sig.disp_offset, sizeof(disp));
    const int64_t target = static_cast<int64_t>(r.offset) + sig.insn_offset + sig.insn_length + disp;
    if (target < 0 || target + static_cast<int64_t>(sizeof(uint64_t)) > static_cast<int64_t>(size)) return std::nullopt;
    return static_cast<uint32_t>(target);
}

// The singleton instance behind the global at `rva`, only when it carries the expected RTTI type. 0 otherwise
// (not created yet, unreadable, or the signature found some other global).
inline uintptr_t readSingleton(const IMemoryReader& reader, uintptr_t image_base, uint32_t global_rva, const SingletonSig& sig) {
    uint64_t instance = 0;
    if (!reader.read(image_base + global_rva, &instance, sizeof(instance)) || instance == 0) return 0;
    const auto object = static_cast<uintptr_t>(instance);
    return objectIsClass(reader, image_base, object, 0, sig.rtti_token) ? object : 0;
}

} // namespace eldenring::live
