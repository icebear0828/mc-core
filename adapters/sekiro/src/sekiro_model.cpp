#include "sekiro_model.hpp"

#include <cstdio>

#include <cstring>

namespace sekiro::live {

namespace {

bool inImage(uintptr_t a, uintptr_t base, size_t size) { return a >= base && a < base + size; }

std::optional<uint64_t> readU64(const IMemoryReader& r, uintptr_t a) {
    uint64_t v = 0;
    if (!r.read(a, &v, sizeof(v))) return std::nullopt;
    return v;
}

std::optional<uint32_t> readU32(const IMemoryReader& r, uintptr_t a) {
    uint32_t v = 0;
    if (!r.read(a, &v, sizeof(v))) return std::nullopt;
    return v;
}

} // namespace

std::optional<std::string> rttiClassName(const IMemoryReader& reader, uintptr_t image_base, size_t image_size, uintptr_t object) {
    const auto vtable = readU64(reader, object);
    if (!vtable || !inImage(*vtable, image_base, image_size)) return std::nullopt;
    const auto col = readU64(reader, *vtable - 8); // complete object locator sits right before the vtable
    if (!col || !inImage(*col, image_base, image_size)) return std::nullopt;
    const auto signature = readU32(reader, *col);
    const auto type_desc_rva = readU32(reader, *col + 12);
    if (!signature || *signature != 1 || !type_desc_rva || *type_desc_rva >= image_size) return std::nullopt;

    // TypeDescriptor: vtable pointer, spare pointer, then the mangled name
    std::string name;
    for (size_t i = 0; i < 128; ++i) {
        char c = 0;
        if (!reader.read(image_base + *type_desc_rva + 16 + i, &c, 1)) return std::nullopt;
        if (c == 0) break;
        name.push_back(c);
    }
    constexpr std::string_view kClassPrefix = ".?AV";
    constexpr std::string_view kStructPrefix = ".?AU";
    if (name.rfind(kClassPrefix, 0) != 0 && name.rfind(kStructPrefix, 0) != 0) return std::nullopt;
    name.erase(0, kClassPrefix.size());
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, "@@") == 0) name.erase(name.size() - 2);
    return name;
}

ModelHider::ModelHider(const IMemoryReader& reader, IMemoryWriter& writer, uintptr_t image_base, size_t image_size,
                       uint32_t world_chr_man_global_rva)
    : reader_(reader), writer_(writer), base_(image_base), size_(image_size), wcm_rva_(world_chr_man_global_rva) {}

std::optional<uintptr_t> ModelHider::resolveEntity() const {
    auto link = [&](uintptr_t address) -> std::optional<uintptr_t> {
        const auto v = readU64(reader_, address);
        if (!v || *v == 0) return std::nullopt;
        return static_cast<uintptr_t>(*v);
    };
    const auto world = link(base_ + wcm_rva_);
    if (!world) return std::nullopt;
    const auto player = link(*world + layout::kPlayerInsInWorldChrMan);
    if (!player) return std::nullopt;
    const auto model = link(*player + layout::kModelInChrIns);
    if (!model) return std::nullopt;
    return link(*model + layout::kAsmDrawEntityInModel);
}

HideStatus ModelHider::updateDrawEntity(bool want_hidden) {
    const auto entity = resolveEntity();
    if (!entity) {
        status_ = HideStatus::NotInWorld;
        return status_;
    }
    if (have_original_ && *entity != entity_) {
        // The remembered value belonged to an object that is no longer the one the game draws with.
        have_original_ = false;
        entity_ = 0;
    }

    const uintptr_t mask_address = *entity + layout::kDrawMask;
    const auto current = readU32(reader_, mask_address);
    if (!current) {
        status_ = HideStatus::NotInWorld;
        return status_;
    }

    if (!want_hidden) {
        if (have_original_) {
            // Only put the value back if it is still the one we wrote; never overwrite what the game set since.
            if (*current == layout::kHiddenMask) {
                const uint32_t original = original_mask_;
                if (!writer_.write(mask_address, &original, sizeof(original))) {
                    status_ = HideStatus::WriteFailed;
                    return status_;
                }
            }
            have_original_ = false;
            entity_ = 0;
        }
        status_ = HideStatus::Idle;
        return status_;
    }

    const auto name = rttiClassName(reader_, base_, size_, *entity);
    if (!name || name->rfind(layout::kAsmDrawEntityClass, 0) != 0) {
        status_ = HideStatus::Rejected;
        return status_;
    }

    if (*current == layout::kHiddenMask) {
        // Already zero: fine if that was us, otherwise we have no known visible value to restore later.
        status_ = have_original_ ? HideStatus::Hidden : HideStatus::Rejected;
        return status_;
    }
    if (*current > layout::kMaxPlausibleMask) {
        status_ = HideStatus::Rejected;
        return status_;
    }

    if (have_original_) ++resets_; // we hid this very entity and the game made it visible again
    if (!have_original_) {
        original_mask_ = *current;
        have_original_ = true;
        entity_ = *entity;
    }
    const uint32_t hidden = layout::kHiddenMask;
    if (!writer_.write(mask_address, &hidden, sizeof(hidden))) {
        status_ = HideStatus::WriteFailed;
        return status_;
    }
    status_ = HideStatus::Hidden;
    return status_;
}

std::optional<uintptr_t> ModelHider::resolveChrModel() const {
    auto link = [&](uintptr_t address) -> std::optional<uintptr_t> {
        uint64_t p = 0;
        if (!reader_.read(address, &p, sizeof(p)) || p == 0) return std::nullopt;
        return static_cast<uintptr_t>(p);
    };
    const auto world = link(base_ + wcm_rva_);
    if (!world) return std::nullopt;
    const auto player = link(*world + layout::kPlayerInsInWorldChrMan);
    if (!player) return std::nullopt;
    const auto model = link(*player + layout::kModelInChrIns);
    if (!model) return std::nullopt;
    const auto vtable = link(*model);
    if (!vtable || *vtable != base_ + layout::kChrModelVtableRva) return std::nullopt;
    return model;
}

HideStatus ModelHider::updateChrModelMasks(bool want_hidden) {
    const auto model = resolveChrModel();
    if (!model) return HideStatus::NotInWorld;
    if (have_masks_original_ && *model != masks_model_) {
        have_masks_original_ = false; // the remembered values belonged to another object
        masks_model_ = 0;
        ++object_changes_;
    }

    uint64_t current[2]{};
    if (!reader_.read(*model + layout::kChrModelDrawMask1, &current[0], sizeof(uint64_t)) ||
        !reader_.read(*model + layout::kChrModelDrawMask2, &current[1], sizeof(uint64_t))) {
        return HideStatus::NotInWorld;
    }
    const bool zero = current[0] == 0 && current[1] == 0;

    if (!want_hidden) {
        if (have_masks_original_) {
            if (zero) { // only put the values back if they are still the ones we wrote
                if (!writer_.write(*model + layout::kChrModelDrawMask1, &masks_original_[0], sizeof(uint64_t)) ||
                    !writer_.write(*model + layout::kChrModelDrawMask2, &masks_original_[1], sizeof(uint64_t))) {
                    return HideStatus::WriteFailed;
                }
            }
            have_masks_original_ = false;
            masks_model_ = 0;
        }
        return HideStatus::Idle;
    }

    if (zero) return have_masks_original_ ? HideStatus::Hidden : HideStatus::Rejected; // not ours to restore
    if (have_masks_original_) ++resets_; // we hid this very object and the game made it visible again
    if (!have_masks_original_) {
        masks_original_[0] = current[0];
        masks_original_[1] = current[1];
        masks_model_ = *model;
        have_masks_original_ = true;
    }
    const uint64_t zero_mask = 0;
    if (!writer_.write(*model + layout::kChrModelDrawMask1, &zero_mask, sizeof(zero_mask))) {
        have_masks_original_ = false;
        masks_model_ = 0;
        return HideStatus::WriteFailed;
    }
    if (!writer_.write(*model + layout::kChrModelDrawMask2, &zero_mask, sizeof(zero_mask))) {
        // never leave the model half hidden
        writer_.write(*model + layout::kChrModelDrawMask1, &masks_original_[0], sizeof(uint64_t));
        have_masks_original_ = false;
        masks_model_ = 0;
        return HideStatus::WriteFailed;
    }
    return HideStatus::Hidden;
}

void ModelHider::restoreChrModelMasks() {
    if (have_masks_original_) {
        const auto model = resolveChrModel();
        if (model && *model == masks_model_) {
            uint64_t current[2]{};
            if (reader_.read(*model + layout::kChrModelDrawMask1, &current[0], sizeof(uint64_t)) &&
                reader_.read(*model + layout::kChrModelDrawMask2, &current[1], sizeof(uint64_t)) && current[0] == 0 && current[1] == 0) {
                writer_.write(*model + layout::kChrModelDrawMask1, &masks_original_[0], sizeof(uint64_t));
                writer_.write(*model + layout::kChrModelDrawMask2, &masks_original_[1], sizeof(uint64_t));
            }
        }
    }
    have_masks_original_ = false;
    masks_model_ = 0;
}

std::string ModelHider::describe() const {
    char buf[320];
    std::string text;
    auto readU64At = [&](uintptr_t a) -> std::optional<uint64_t> {
        uint64_t v = 0;
        if (!reader_.read(a, &v, sizeof(v))) return std::nullopt;
        return v;
    };
    const auto world = readU64At(base_ + wcm_rva_);
    const auto player = world && *world ? readU64At(*world + layout::kPlayerInsInWorldChrMan) : std::nullopt;
    const auto model = player && *player ? readU64At(*player + layout::kModelInChrIns) : std::nullopt;
    if (!model || *model == 0) return "model=none";
    const auto vtable = readU64At(*model);
    if (!vtable || *vtable != base_ + layout::kChrModelVtableRva) {
        text = "model=wrong-class";
    } else {
        const auto m1 = readU64At(*model + layout::kChrModelDrawMask1);
        const auto m2 = readU64At(*model + layout::kChrModelDrawMask2);
        std::snprintf(buf, sizeof(buf), "model=%llx masks=%llx,%llx", static_cast<unsigned long long>(*model),
                      static_cast<unsigned long long>(m1.value_or(0)), static_cast<unsigned long long>(m2.value_or(0)));
        text = buf;
    }
    const auto entity = readU64At(*model + layout::kAsmDrawEntityInModel);
    if (!entity || *entity == 0 || *entity < 0x10000) {
        text += " entity=none";
    } else {
        const auto mask = readU32(reader_, *entity + layout::kDrawMask);
        std::snprintf(buf, sizeof(buf), " entity=%llx entity_mask=%u", static_cast<unsigned long long>(*entity), mask.value_or(0xFFFFFFFFu));
        text += buf;
    }
    return text;
}

HideStatus ModelHider::update(bool want_hidden) {
    // Two independent ways to hide the same mesh; each reports NotInWorld when it cannot find its object.
    const HideStatus entity = updateDrawEntity(want_hidden);
    const HideStatus masks = updateChrModelMasks(want_hidden);

    if (entity == HideStatus::NotInWorld && masks == HideStatus::NotInWorld) {
        status_ = HideStatus::NotInWorld;
    } else if (entity == HideStatus::Hidden || masks == HideStatus::Hidden) {
        status_ = HideStatus::Hidden;
    } else if (entity == HideStatus::WriteFailed || masks == HideStatus::WriteFailed) {
        status_ = HideStatus::WriteFailed;
    } else if (entity == HideStatus::Rejected || masks == HideStatus::Rejected) {
        status_ = HideStatus::Rejected;
    } else {
        status_ = HideStatus::Idle;
    }
    return status_;
}

void ModelHider::restore() {
    restoreChrModelMasks();
    restoreDrawEntity();
    status_ = HideStatus::Idle;
}

void ModelHider::restoreDrawEntity() {
    if (have_original_) {
        const auto entity = resolveEntity();
        if (entity && *entity == entity_) {
            const auto current = readU32(reader_, *entity + layout::kDrawMask);
            if (current && *current == layout::kHiddenMask) {
                const uint32_t original = original_mask_;
                writer_.write(*entity + layout::kDrawMask, &original, sizeof(original));
            }
        }
    }
    have_original_ = false;
    entity_ = 0;
}

void ModelHider::forgetObject() {
    have_original_ = false;
    entity_ = 0;
    have_masks_original_ = false;
    masks_model_ = 0;
    status_ = HideStatus::NotInWorld;
}

} // namespace sekiro::live
