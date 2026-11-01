#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

void McFlagsStep() {
    using namespace eldenring::flags;
    static ByteFlagHold stamina_hold, death_hold;
    static ByteValueHold hud_hold;
    static uintptr_t held_data = 0, held_menu = 0;
    static bool logged_stamina = false, logged_death = false, logged_hud = false;
    static uint64_t last_status_ms = 0;
    static const bool enabled = !FileExists(g_game_dir + "mc_er_noflags.txt");
    const FlagPolicy policy = policyFor(g_mc_mode.load(std::memory_order_relaxed), CurrentGameMode() == creative::GameMode::Creative, enabled);

    const uintptr_t player = PlayerChrPtr();
    const uintptr_t data = player != 0 ? DataModuleOfChr(player) : 0;
    if (data != held_data) { // another module (a load, a respawn): the old one is gone, start fresh
        stamina_hold = ByteFlagHold{};
        death_hold = ByteFlagHold{};
        held_data = data;
    }
    if (data != 0) {
        uint8_t b = 0;
        if (SafeCopy(data + kDataFlagsByte, &b, 1)) {
            if (const auto w = stamina_hold.step(b, policy.no_stamina, kNoStaminaBit)) { SafeWrite8(data + kDataFlagsByte, *w); b = *w; }
            if (const auto w = death_hold.step(b, policy.cannot_die, kCannotDieBit)) { SafeWrite8(data + kDataFlagsByte, *w); b = *w; }
        }
    }
    const uintptr_t menu = readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan);
    if (menu != held_menu) {
        hud_hold = ByteValueHold{};
        held_menu = menu;
    }
    if (menu != 0) {
        uint8_t b = 0;
        if (SafeCopy(menu + kMenuHudSettingByte, &b, 1)) {
            if (const auto w = hud_hold.step(b, policy.hide_hud, 0)) SafeWrite8(menu + kMenuHudSettingByte, *w);
        }
    }
    if (menu != 0 && g_mc_mode.load(std::memory_order_relaxed)) { // read-only: the fade states of the native HUD updater (0x1407733F0), logged when one changes
        struct HudSeen { int32_t a, b, c, d; uint8_t e, f, g, h, i; };
        static HudSeen last{-99, -99, -99, -99, 9, 9, 9, 9, 9};
        HudSeen now{};
        SafeCopy(menu + 0x6530, &now.a, 4);
        SafeCopy(menu + 0x6534, &now.b, 4);
        SafeCopy(menu + 0x6538, &now.c, 4);
        SafeCopy(menu + 0x6548, &now.d, 4);
        SafeCopy(menu + 0x6540, &now.e, 1);
        SafeCopy(menu + 0x6541, &now.f, 1);
        SafeCopy(menu + 0x6542, &now.g, 1);
        SafeCopy(menu + 0x654C, &now.h, 1);
        SafeCopy(menu + kMenuHudSettingByte, &now.i, 1);
        if (now.a != last.a || now.b != last.b || now.c != last.c || now.d != last.d || now.e != last.e || now.f != last.f || now.g != last.g || now.h != last.h || now.i != last.i) {
            Log("HUDPROBE: +6530=%d +6534=%d +6538=%d +6548=%d b6540=%u b6541=%u b6542=%u b654C=%u setting841C=%u", now.a, now.b, now.c, now.d, now.e, now.f, now.g, now.h, now.i);
            last = now;
        }
    }
    if (stamina_hold.active() != logged_stamina) { logged_stamina = stamina_hold.active(); Log("flags: no-stamina bit %s", logged_stamina ? "held" : "released"); }
    if (death_hold.active() != logged_death) { logged_death = death_hold.active(); Log("flags: cannot-die bit %s", logged_death ? "held" : "released"); }
    if (hud_hold.active() != logged_hud) { logged_hud = hud_hold.active(); Log("flags: native HUD setting byte %s", logged_hud ? "forced to 0" : "restored"); }
    const uint64_t now = GetTickCount64();
    if ((stamina_hold.active() || death_hold.active() || hud_hold.active()) && now - last_status_ms > 3000) {
        last_status_ms = now;
        uint8_t fb = 0, hb = 0;
        int32_t sp = -1, sp_max = -1;
        if (data != 0) {
            SafeCopy(data + kDataFlagsByte, &fb, 1);
            SafeCopy(data + kDataStamina, &sp, sizeof(sp));
            SafeCopy(data + kDataMaxStamina, &sp_max, sizeof(sp_max));
        }
        if (menu != 0) SafeCopy(menu + kMenuHudSettingByte, &hb, 1);
        Log("FLAGS: data+0x19A=0x%02X stamina=%d/%d hud_setting=%u", fb, sp, sp_max, hb);
    }
}

} // namespace eldenring::loader_detail
