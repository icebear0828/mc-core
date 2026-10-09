#pragma once

// Signature scanning and the loader's environment self-check. Platform independent (works on a byte buffer), so
// it is unit-tested on any host. Rule for every signature: bind only on EXACTLY ONE match; zero or several
// matches mean the capability stays off (fail closed). The signatures were verified unique in the 2.7.1.0 image
// only; they are not evidence for other builds.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eldenring::live {

class Signature {
public:
    // "48 8B ?? 05": single-space separated, each token two hex digits or "??" (wildcard).
    static std::optional<Signature> parse(std::string_view text) {
        std::vector<int16_t> bytes;
        size_t i = 0;
        while (i < text.size()) {
            if (i + 2 > text.size()) return std::nullopt;
            const std::string_view tok = text.substr(i, 2);
            if (tok == "??") {
                bytes.push_back(-1);
            } else {
                const int hi = hex(tok[0]), lo = hex(tok[1]);
                if (hi < 0 || lo < 0) return std::nullopt;
                bytes.push_back(static_cast<int16_t>(hi * 16 + lo));
            }
            i += 2;
            if (i == text.size()) break;
            if (text[i] != ' ') return std::nullopt;
            ++i;
            if (i == text.size()) return std::nullopt; // trailing space
        }
        if (bytes.empty()) return std::nullopt;
        Signature s;
        s.bytes_ = std::move(bytes);
        return s;
    }

    [[nodiscard]] size_t size() const { return bytes_.size(); }
    [[nodiscard]] bool matchesAt(const uint8_t* data, size_t at) const {
        for (size_t k = 0; k < bytes_.size(); ++k) {
            if (bytes_[k] >= 0 && data[at + k] != static_cast<uint8_t>(bytes_[k])) return false;
        }
        return true;
    }

private:
    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    std::vector<int16_t> bytes_;
};

enum class ScanStatus { Unique, NotFound, Ambiguous };

struct ScanResult {
    ScanStatus status{ScanStatus::NotFound};
    size_t offset{0}; // valid only when Unique
};

// Counts every (also overlapping) match; Unique only when there is exactly one.
inline ScanResult scanUnique(const uint8_t* data, size_t size, const Signature& sig) {
    ScanResult r;
    if (sig.size() == 0 || size < sig.size()) return r;
    size_t matches = 0;
    for (size_t at = 0; at + sig.size() <= size; ++at) {
        if (!sig.matchesAt(data, at)) continue;
        if (++matches == 1) r.offset = at;
        if (matches > 1) {
            r.status = ScanStatus::Ambiguous;
            return r;
        }
    }
    r.status = matches == 1 ? ScanStatus::Unique : ScanStatus::NotFound;
    return r;
}

namespace sigs {
// docs/ELDENRING_REVERSE.md 1.6 (raycast) and 1.11 (team relation). 2.7.1.0 only.
inline constexpr const char* kRaycastWrapper =
    "4C 8B DC 53 57 48 81 EC 88 00 00 00 41 0F 28 01 4D 8D 4B A8 41 0F 28 08 4D 8D 43 B8 48 8B 84 24 D0 00 00 00";
inline constexpr const char* kRaycastCore =
    "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 38 FF FF FF 48 81 EC 90 01 00 00 48 C7 44 24 30 FE FF FF FF";
inline constexpr const char* kCheckTeamHostile =
    "48 83 EC 28 0F B6 01 45 33 C0 48 6B C8 4F 0F B6 02 48 8D 54 24 30 48 03 C8 66 C7 44 24 30 01 00";
inline constexpr const char* kCheckTeamFriend =
    "48 83 EC 28 0F B6 01 45 33 C0 48 6B C8 4F 0F B6 02 48 8D 54 24 30 48 03 C8 66 C7 44 24 30 00 01";
// CSChrDataModule::SetMaxHPAndClampHP (0x140438870): a call-free leaf that every character data module runs once per
// update; on the main thread it runs inside CSChrDataModule::Update. 32 bytes, unique in 2.7.1.0.
inline constexpr const char* kClampHp =
    "89 54 24 10 C7 44 24 18 FF FF 07 00 C7 44 24 08 01 00 00 00 83 FA 01 7D 07 48 8D 44 24 08 EB 14";
// ProcessDamageContext (0x140448910): (damageModule, attackerChrIns, hitContext*, u32, u8). Prologue up to the stack-cookie
// load, then `mov r14, r8` (ctx). Both halves are unique in 2.7.1.0 (checked in the exe).
inline constexpr const char* kProcessDamageContext =
    "4C 8B DC 55 53 56 57 41 56 41 57 49 8D 6B 88 48 81 EC 48 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 20 4D 89 63 20 4D 8B F0";
// The function that copies ChrCam's right/up/forward/position into another camera context ([rbx+0x80..0xB0]); it is called
// through a vtable (0x1404A7190). Unique in 2.7.1.0. Used by the first-person experiment only.
inline constexpr const char* kRenderCameraCopy =
    "40 53 48 83 EC 20 48 8B D9 48 83 C1 48 E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 85 C0 74 38 48 8B 80 E0 EC 01 00 48 85 C0 74 2C 0F 28 40 10 "
    "0F 29 83 80 00 00 00";
// The hit VFX spawner (0x140450120): (damageModule, attackerChr, hitContext*, flags) -> bool. Its first gate is
// IsMainPlayer(victim), so it only ever makes the effect for hits the player takes. Unique in 2.7.1.0.
inline constexpr const char* kHitVfxSpawn = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 49 8B F9 49 8B D8 48 8B EA 48 8B F1";
// CSChrDataModule::SetHP(module, hp, flag) = ApplyHPChange (0x140437450): writes [module+0x138], redraws the bars, clamps to the
// maximum. 48 bytes, no relative operands, unique in 2.7.1.0. Must be called on the game thread.
inline constexpr const char* kApplyHpChange =
    "48 89 5C 24 18 48 89 6C 24 20 89 54 24 10 56 57 41 56 48 83 EC 30 8B A9 38 01 00 00 48 8D B9 3C 01 00 00 45 33 F6 0F 29 74 24 20 44 89 74 24 50";
inline constexpr const char* kGetEffectiveTeamType = "48 89 5C 24 10 57 48 83 EC 20 0F B6 41 6C 48 8B F9 88 02";
} // namespace sigs

// ---- loader environment self-check ----------------------------------------------------------------------
// Memory access in a game that runs EasyAntiCheat risks a ban. The loader refuses to bind when an EAC module is
// loaded or `steam_appid.txt` is missing (the offline launch). The process name is NOT checked: the offline
// test process is called start_protected_game.exe.
enum class EnvVerdict { Ok, EacLoaded, MissingSteamAppId };

inline EnvVerdict checkEnvironment(const std::vector<std::string>& loaded_module_names, bool steam_appid_present) {
    for (const auto& name : loaded_module_names) {
        std::string lower;
        lower.reserve(name.size());
        for (char c : name) lower.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        if (lower.find("easyanticheat") != std::string::npos) return EnvVerdict::EacLoaded;
    }
    return steam_appid_present ? EnvVerdict::Ok : EnvVerdict::MissingSteamAppId;
}

} // namespace eldenring::live
