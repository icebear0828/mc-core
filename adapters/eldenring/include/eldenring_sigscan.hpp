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
// The camera update that copies the active camera mode's matrix into ChrCam each frame (0x1403B11D0, found with a hardware write
// watch on ChrCam+0x40: the movaps at 0x3B1929 is the writer). Signature (ChrCam* this, float dt in xmm1, ChrIns*, bool).
// 62 bytes, one match in 2.7.1.0; the stack cookie address is wildcarded.
inline constexpr const char* kCameraStepExecute =
    "4C 8B DC 55 56 57 41 54 41 55 41 56 49 8D 6B A1 48 81 EC F8 00 00 00 45 0F 29 43 98 45 0F 29 4B 88 48 8B 05 ?? ?? ?? ?? 48 33 C4 "
    "48 89 45 D7 49 8B 80 90 01 00 00 48 8B F9 49 89 5B 20 49 8B F0";
// The input master gate (0x14067B020): reads [[global]+0xC34]; movement, roll, attack and the input dispatch all ask it. 15 bytes,
// one match in 2.7.1.0; six callers (REVERSE 12).
inline constexpr const char* kIsInputBlocked = "48 8B 05 ?? ?? ?? ?? 0F B6 80 34 0C 00 00 C3";
// The camera-rotation freeze test (0x140766C60): true when a full-screen menu is open. An identical twin at 0x140766BC0 differs only
// in its call displacements, so the first call's literal displacement is part of the signature. 33 bytes, one match.
inline constexpr const char* kMenuFreezesCamera = "40 53 48 83 EC 20 48 8B D9 BA 3D 00 00 00 48 8D 4C 24 38 E8 F8 1F 00 00 0F B7 00 66 83 F8 47 73 12";
// The hit-reaction pair (REVERSE 21): called from the damage driver 0x449D30 and from 0x447810 (all eight call sites), they pick the
// reaction animation ids (written into ctx+0x220..0x226) and then call the action module's vfunc[8]. 48 bytes each, one match each.
inline constexpr const char* kHitReactDefault =
    "48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 20 48 8B 01 4C 8B E2 8B 5C 24 70 4D 8B F1 8B D3 4D 8B F8 48 8B F9 FF 50 10 8B D3";
inline constexpr const char* kHitReactHeavy =
    "4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 78 48 8B 01 49 8B F1 44 8B 75";
// ChrIns kill (0x1403FCD90): sets the character's hit points to 0 through ApplyHpChange and starts the death. It is what ends a fall from
// a great height (no hit context, so the damage pipeline never sees it). Four callers. 56 bytes, one match.
inline constexpr const char* kKillChr =
    "40 53 48 83 EC 40 C7 44 24 50 00 00 00 00 48 8B D9 48 8B 89 90 01 00 00 0F 57 DB F3 0F 10 05 ?? ?? ?? ?? 45 33 C0 C6 44 24 28 01 33 D2 F3 0F 11 44 24 20 48 8B 09 E8";
// Creative-mode probes (log-only, see eldenring_creative.hpp). Each is unique in 2.7.1.0 (checked in the exe), displacements wildcarded.
// Kill wrapper 0x1403EDA70: a near-identical sibling sits at 0x1403EDB60, so the literal jne displacement (0xB6) is part of the signature.
inline constexpr const char* kKillWrapper =
    "40 53 48 83 EC 40 48 C7 44 24 20 FE FF FF FF 0F 29 74 24 30 48 8B D9 48 8B 41 58 48 8B 90 C8 00 00 00 F6 42 24 01 0F 85 B6 00 00 00 E8 ?? ?? ?? ?? "
    "8B 43 68 83 F8 03";
// Fall height 0x14044E240 (rcx = FallModule*, float result).
inline constexpr const char* kFallHeight =
    "48 83 EC 58 0F 29 74 24 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 80 79 1D 00 48 89 7C 24 50 48 8B F9 74 0A";
// Character event dispatcher 0x140428DE0 (this, event*): switches on the event type; types 12/47 call the kill wrappers, 46 sets a state bit,
// 48/126 clean up or tear down. 45 bytes, no wildcard needed, one match in 2.7.1.0.
inline constexpr const char* kChrEventDispatch =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 72 08 33 C0 0F B6 7A 18 48 8B 59 18 0F B7 56 0E 66 3B C2 73 14 48 8B 8B 78 01 00 00";
// 0x14044E3A0 bool (FallModule* this, float threshold in xmm1): [this+0x18] (the time in the air) > threshold, with two more checks. One caller
// (0x1404117EB, inside the fall damage evaluator). 28 bytes, one match in 2.7.1.0.
inline constexpr const char* kFallTimeExceeded = "48 89 5C 24 08 57 48 83 EC 20 F3 0F 10 41 18 48 8B F9 0F 2F C1 0F 97 C3 84 DB 74 72";
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
