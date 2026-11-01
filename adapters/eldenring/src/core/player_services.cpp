#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

uint64_t NowTick() { return GetTickCount64() * 60ull / 1000ull; } // 1/60 s units

bool CallVfunc7(uintptr_t damage_module, uintptr_t attacker, void* ctx) {
    __try {
        void** vtable = *reinterpret_cast<void***>(damage_module);
        auto fn = reinterpret_cast<DamageFn>(vtable[layout::kDamageVfuncIndex]);
        fn(reinterpret_cast<void*>(damage_module), reinterpret_cast<void*>(attacker), ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ReadHurt(float& hurt, float& side) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    hurt = g_feedback.hurt();
    side = g_feedback.hurtSide();
}

bool IsPlayerActionModule(void* self) {
    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return false;
    uint64_t container = 0, action = 0;
    if (!SafeCopy(player + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !SafeCopy(static_cast<uintptr_t>(container) + 0xA0, &action, sizeof(action))) {
        return false;
    }
    return static_cast<uintptr_t>(action) == reinterpret_cast<uintptr_t>(self);
}

uintptr_t ModuleOwner(void* fall_module) {
    uint64_t owner = 0;
    return SafeCopy(reinterpret_cast<uintptr_t>(fall_module) + 8, &owner, sizeof(owner)) ? static_cast<uintptr_t>(owner) : 0;
}

bool CreativeShouldLogFall(float metres, uintptr_t caller_rva) {
    std::lock_guard<std::mutex> lock(g_cr_mutex);
    return creative::shouldLogFall(g_cr_fall_state, metres, caller_rva, GetTickCount64());
}

bool WriteBytesSafe(uintptr_t address, const void* src, size_t n) {
    if (address < 0x10000) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

RayResult CastStaticRay(const float from[3], const float disp[3]) {
    // The Havok world is not queryable while a menu or loading screen owns it; a ray there has
    // crashed the game in a null-pointer read during the load transition.
    if (!WantSuppress()) return {};
    return eldenring::combat::CombatService::Instance().castStaticRay(from, disp);
}

bool PlayerCanSee(const float player_feet[3], const float victim_feet[3]) {
    return eldenring::combat::CombatService::Instance().playerCanSee(player_feet, victim_feet);
}

const char* OutcomeName(DamageOutcome o) {
    switch (o) {
        case DamageOutcome::Applied: return "Applied";
        case DamageOutcome::Deferred: return "Deferred";
        case DamageOutcome::Expired: return "Expired";
        case DamageOutcome::NoTemplate: return "NoTemplate";
        case DamageOutcome::NoPlayer: return "NoPlayer";
        case DamageOutcome::VictimGone: return "VictimGone";
        case DamageOutcome::WrongModule: return "WrongModule";
        case DamageOutcome::VictimDead: return "VictimDead";
        case DamageOutcome::NotHostile: return "NotHostile";
        case DamageOutcome::BadDamage: return "BadDamage";
        case DamageOutcome::NoLineOfSight: return "NoLineOfSight";
    }
    return "?";
}

int32_t HpOf(uintptr_t chr) {
    Vitals v;
    return readVitals(g_reader, g_img.base, chr, v) ? v.hp : -1;
}

void LogHitGeometry(const char* tag, void* attacker, uintptr_t player, const uint8_t* ctx) {
    eldenring::debug::LogHitGeometry(tag, attacker, player, ctx, g_reader, g_img.base);
}

void LogPlayerHit(void* module, void* attacker, const uint8_t* ctx, uint8_t blocked_flag) {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::LogPlayerHit(module, attacker, ctx, blocked_flag, g_reader, g_img.base, world);
}

void LogChrScan(uintptr_t chr, size_t index) {
    eldenring::debug::LogChrScan(chr, index, g_reader, g_img.base, PlayerChrPtr());
}

uintptr_t PlayerChrPtr() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t p = 0;
    return world != 0 && SafeCopy(world + layout::kPlayerInsInWorldChrMan, &p, sizeof(p)) ? static_cast<uintptr_t>(p) : 0;
}

bool PlayerDead() {
    const uintptr_t player = PlayerChrPtr();
    Vitals v;
    return player != 0 && readVitals(g_reader, g_img.base, player, v) && v.max_hp > 0 && v.hp <= 0;
}

uintptr_t DataModuleOfChr(uintptr_t chr) {
    uint64_t container = 0, data = 0;
    if (chr == 0 || !SafeCopy(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !SafeCopy(static_cast<uintptr_t>(container) + layout::kChrDataModuleSlot * sizeof(uint64_t), &data, sizeof(data))) {
        return 0;
    }
    return static_cast<uintptr_t>(data);
}

bool GiveHp(uintptr_t data_module, int amount) {
    if (!g_apply_hp || data_module == 0 || amount <= 0) return false;
    int32_t hp = 0, max_hp = 0;
    if (!SafeCopy(data_module + layout::kDataHp, &hp, sizeof(hp)) || !SafeCopy(data_module + layout::kDataMaxHp, &max_hp, sizeof(max_hp)) || hp <= 0 ||
        max_hp <= 0) {
        return false;
    }
    __try {
        g_apply_hp(reinterpret_cast<void*>(data_module), std::min(max_hp, hp + amount), 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_apply_hp = nullptr; // never call it again after a fault
        return false;
    }
}

bool SetChrHp(uintptr_t chr, int32_t new_hp) {
    if (chr == 0) return false;
    const uintptr_t data = DataModuleOfChr(chr);
    if (data == 0) return false;
    if (g_apply_hp) {
        __try {
            g_apply_hp(reinterpret_cast<void*>(data), new_hp, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_apply_hp = nullptr;
        }
    }
    SafeWrite32(data + layout::kDataHp, new_hp);
    return true;
}

bool KillChr(uintptr_t chr) {
    return SetChrHp(chr, 0);
}

bool WantSuppress() {
    const auto& mode = eldenring::RuntimeContext::Instance().mode();
    if (!g_mc_mode.load() && !mode.cs_mode.load() && !mode.ow_mode.load()) return false;
    MenuState ms;
    LoadingState ls;
    if (!readMenuState(g_reader, readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan), ms) ||
        !readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls)) {
        return false;
    }
    return !ms.menu_focused && !ms.popup_open && !ls.screen_loading;
}

bool PlayerAirborne() {
    const uintptr_t w = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t p = 0;
    return w != 0 && SafeCopy(w + layout::kPlayerInsInWorldChrMan, &p, sizeof(p)) && p != 0 &&
           detail::readAirborne(g_reader, g_img.base, static_cast<uintptr_t>(p));
}

void LogPhysicsVectors() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::LogPhysicsVectors(g_reader, g_img.base, world);
}

void LogPlayerPoseForBuddy() {
    eldenring::debug::LogPlayerPoseForBuddy(g_reader, g_img.base, PlayerChrPtr());
}

void LogGroundBytesOnChange() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::LogGroundBytesOnChange(g_reader, g_img.base, world);
}

void SnapshotNativeParts() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::SnapshotNativeParts(g_reader, g_img.base, world, g_ctx, g_hidden_flags, g_native_mutex);
}

void ResetSlotProbe(int& cursor) {
    eldenring::debug::ResetSlotProbe(cursor, g_hide_slots, g_slot_probe, g_slots_changed, [](const char* s) { Log("%s", s); });
}

void CycleHiddenSlot(int& cursor) {
    const uintptr_t player = PlayerChrPtr();
    eldenring::debug::CycleHiddenSlot(cursor, player, g_reader, g_img.base, g_hide_slots, g_slot_probe, g_slots_changed);
}

} // namespace eldenring::loader_detail
