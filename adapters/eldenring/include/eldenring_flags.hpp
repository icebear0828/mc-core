#pragma once

// Game state bytes the MC mode holds while it is on and gives back when it is off (REVERSE 40/41, bytes checked in the exe).
// Pure logic: the loader reads the byte, asks `step` what to write, and writes it.

#include <cstdint>
#include <optional>

namespace eldenring::flags {

// CSChrDataModule (the player's, module container slot 0) flag byte +0x19A:
//   0x40: the stamina setter 0x1404389F0 skips the deduction (`test byte [rcx+0x19A],0x40`, with r8b == 0);
//   0x80: 0x140437CA0 answers "cannot die", so the HP setter wrapper 0x140438810 clamps a lethal new HP to 1 (it does not stop KillChr 0x1403FCD90,
//         which calls the SetHP core directly).
// The game itself rewrites this byte (bits 0x10/0x20 are copied from ChrIns+0x1C7 at 0x1404395F0) with a read-modify-write, so the bits are re-forced every tick.
inline constexpr uintptr_t kDataFlagsByte = 0x19A;
inline constexpr uint8_t kNoStaminaBit = 0x40;
inline constexpr uint8_t kCannotDieBit = 0x80;
// CSMenuMan+0x841C: the HUD display setting (1 = shown; set from the option at 0x14076B96A, read once at 0x14077351D where 0 forces the hidden state).
inline constexpr uintptr_t kMenuHudSettingByte = 0x841C;
// The current and maximum stamina (CSChrDataModule +0x154 / +0x158, int32), for the log.
inline constexpr uintptr_t kDataStamina = 0x154;
inline constexpr uintptr_t kDataMaxStamina = 0x158;

// What is held when. MC has no stamina and no native HUD in any mode. The can't-die bit is only for creative: the HP-delta wrapper at 0x140436AF0
// (0x140436B30) asks the same 0x140437CA0, so in survival the bit would also keep the player alive through ordinary hits.
struct FlagPolicy {
    bool no_stamina{false};
    bool cannot_die{false};
    bool hide_hud{false};
};
inline FlagPolicy policyFor(bool mc_mode, bool creative, bool enabled) {
    if (!mc_mode || !enabled) return {};
    return {true, creative, true};
}

// Forces `mask` bits on in a byte and remembers which of them were off, so releasing clears only those.
class ByteFlagHold {
public:
    // `current` is the byte as read now. Returns the byte to write, or nothing when it is already right.
    [[nodiscard]] std::optional<uint8_t> step(uint8_t current, bool want, uint8_t mask) {
        if (want) {
            if (!active_) {
                saved_ = static_cast<uint8_t>(current & mask);
                active_ = true;
            }
            const uint8_t desired = static_cast<uint8_t>(current | mask);
            if (desired == current) return std::nullopt;
            return desired;
        }
        if (!active_) return std::nullopt;
        active_ = false;
        const uint8_t desired = static_cast<uint8_t>((current & ~mask) | saved_);
        if (desired == current) return std::nullopt;
        return desired;
    }
    [[nodiscard]] bool active() const { return active_; }

private:
    bool active_{false};
    uint8_t saved_{0};
};

// Forces a whole byte to a value and restores what it was before the first force.
class ByteValueHold {
public:
    [[nodiscard]] std::optional<uint8_t> step(uint8_t current, bool want, uint8_t value) {
        if (want) {
            if (!active_) {
                saved_ = current;
                active_ = true;
            }
            if (current == value) return std::nullopt;
            return value;
        }
        if (!active_) return std::nullopt;
        active_ = false;
        if (current == saved_) return std::nullopt;
        return saved_;
    }
    [[nodiscard]] bool active() const { return active_; }

private:
    bool active_{false};
    uint8_t saved_{0};
};

} // namespace eldenring::flags
