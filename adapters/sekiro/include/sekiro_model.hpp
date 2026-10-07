#pragma once

// Hiding the native Wolf model by writing the draw mask of its assembled-model draw entity.
//
// Found on a running game (2026-10-07): [[[WorldChrMan+0x88]+0x48]+0x250] is a SprjAsmModelDrawEntity
// (RTTI) that draws the whole Wolf; its int at +0x70 is a draw mask (4 while visible). Writing 0 makes the
// model, weapons and shadow vanish; restoring the value brings it back. The mask is only ever written
// after the object's RTTI class and the current value pass validation.

#include "sekiro_live.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sekiro::live {

class IMemoryWriter {
public:
    virtual ~IMemoryWriter() = default;
    // Must fail (return false) instead of faulting on unwritable memory.
    virtual bool write(uintptr_t address, const void* data, size_t size) = 0;
};

namespace layout {
inline constexpr uintptr_t kModelInChrIns = 0x48;          // ChrIns -> ChrModel
inline constexpr uintptr_t kAsmDrawEntityInModel = 0x250;  // ChrModel -> SprjAsmModelDrawEntity
inline constexpr uintptr_t kDrawMask = 0x70;               // int, 4 while drawn
inline constexpr uint32_t kHiddenMask = 0;
inline constexpr uint32_t kMaxPlausibleMask = 0xFF;        // a visible mask is a small non-zero number
inline constexpr std::string_view kAsmDrawEntityClass = "SprjAsmModelDrawEntity";
// ChrModel (RTTI vtable RVA) carries two 64-bit draw masks, all ones while drawn (user-verified).
inline constexpr uint32_t kChrModelVtableRva = 0x29F7CE8;
inline constexpr uintptr_t kChrModelDrawMask1 = 0x90;
inline constexpr uintptr_t kChrModelDrawMask2 = 0x98;
} // namespace layout

// MSVC RTTI class name of a polymorphic object, e.g. "SprjAsmModelDrawEntity@NS_SPRJ"; nullopt when the
// object has no valid vtable / RTTI chain inside the image.
std::optional<std::string> rttiClassName(const IMemoryReader& reader, uintptr_t image_base, size_t image_size, uintptr_t object);

enum class HideStatus {
    Idle,        // visible (or nothing to do)
    Hidden,      // mask is zero
    NotInWorld,  // no player / model yet
    Rejected,    // the object is not what we expect; nothing was written
    WriteFailed, // the write itself failed
};

class ModelHider {
public:
    ModelHider(const IMemoryReader& reader, IMemoryWriter& writer, uintptr_t image_base, size_t image_size,
               uint32_t world_chr_man_global_rva);

    // Call every frame while the game link is up. Idempotent: hides or restores only when needed.
    HideStatus update(bool want_hidden);

    // Best effort: put the original mask back (used when the mod unloads).
    void restore();

    // The world was left: the entity is gone, so forget the remembered value without touching memory.
    void forgetObject();

    [[nodiscard]] HideStatus status() const { return status_; }

private:
    std::optional<uintptr_t> resolveEntity() const;
    std::optional<uintptr_t> resolveChrModel() const; // class-checked by vtable
    HideStatus updateDrawEntity(bool want_hidden);    // NotInWorld = this path is unavailable
    HideStatus updateChrModelMasks(bool want_hidden); // NotInWorld = this path is unavailable
    void restoreChrModelMasks();
    void restoreDrawEntity();

    const IMemoryReader& reader_;
    IMemoryWriter& writer_;
    uintptr_t base_;
    size_t size_;
    uint32_t wcm_rva_;
    HideStatus status_{HideStatus::Idle};
    uintptr_t entity_{0};            // entity we hid (the remembered value belongs to it)
    uint32_t original_mask_{0};
    bool have_original_{false};
    uintptr_t masks_model_{0};       // the ChrModel whose masks we zeroed
    uint64_t masks_original_[2]{0, 0};
    bool have_masks_original_{false};
};

} // namespace sekiro::live
