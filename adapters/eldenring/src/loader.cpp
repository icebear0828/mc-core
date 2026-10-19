// Elden Ring loader (milestone 1, step 1+2): a dinput8.dll proxy.
//   - always: logs the environment, binds the singletons by signature + RTTI, and logs a status line once a second
//     (read-only, nothing in the game is touched);
//   - if `mc_er_damage.txt` sits in the game folder: hooks CSChrDataModule::SetMaxHPAndClampHP and drains the damage
//     queue from it on the game thread; F8 queues a hit on the nearest hostile enemy (needs `mc_er_hit.bin`).
// Offline only: the loader refuses to do anything when an EAC module is loaded or steam_appid.txt is missing.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <unknwn.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <share.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "mc/contracts/combat_adapter.hpp"

#include "input_hook.hpp"
#include "overlay_d3d12.hpp"
#include "png_wic.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_damage.hpp"
#include "audio_xaudio2.hpp"
#include "eldenring_audio_data.hpp"
#include "eldenring_blockmesh.hpp"
#include "eldenring_blocks.hpp"
#include "eldenring_mining.hpp"
#include "eldenring_tnt.hpp"
#include "eldenring_creeper.hpp"

#include "eldenring_buddy.hpp"
#include "eldenring_chrscan.hpp"
#include "eldenring_mobs.hpp"
#include "eldenring_virtual_summon.hpp"
#include "eldenring_creative.hpp"
#include "eldenring_flight.hpp"
#include "eldenring_los.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_bullet.hpp"
#include "eldenring_shadow.hpp"
#include "eldenring_survival.hpp"
#include "eldenring_xp.hpp"
#include "eldenring_hunger.hpp"
#include "eldenring_jump.hpp"
#include "eldenring_sprint.hpp"
#include "eldenring_orientation.hpp"
#include "eldenring_action_lock.hpp"
#include "eldenring_roll.hpp"
#include "mc/consumables.hpp"
#include "mc/inventory_layout.hpp"
#include "mc/hud_atlas.hpp"
#include "mc/inventory.hpp"
#include "eldenring_melee.hpp"
#include "eldenring_shield.hpp"
#include "eldenring_flags.hpp"
#include "eldenring_particles.hpp"
#include "eldenring_model.hpp"
#include "eldenring_pick.hpp"
#include "eldenring_entity_manager.hpp"
#include "eldenring_steve.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_state.hpp"
#include "eldenring_world.hpp"

using namespace eldenring::live;
namespace blocks = eldenring::blocks;
namespace creative = eldenring::creative;
namespace flight = eldenring::flight;

#include "eldenring_diagnostics.hpp"
#include "eldenring_locomotion.hpp"
#include "eldenring_world_runtime.hpp"
#include "eldenring_combat_service.hpp"
#include "eldenring_memory_scanner.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_inventory_controller.hpp"
#include "eldenring_debug_probes.hpp"
#include "eldenring_hud_bridge.hpp"
#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_game_hooks.hpp"

namespace {

using eldenring::diagnostics::Log;
using eldenring::diagnostics::InitLogOnce;
using eldenring::diagnostics::CrashVehHandler;

const auto& g_game_dir = eldenring::diagnostics::GetGameDir();

using eldenring::memory::SehGuard;
using eldenring::memory::SafeCopy;
using eldenring::memory::SafeWrite;
using eldenring::memory::SafeWrite8;
using eldenring::memory::SafeWrite32;
using eldenring::memory::SelfReader;
using eldenring::memory::ImageInfo;
using eldenring::memory::ReadImageInfo;
using eldenring::memory::Bound;
using eldenring::memory::LocateSingleton;
using eldenring::memory::LocateByPrefix;

const SelfReader g_reader;

// ---- game state ----------------------------------------------------------------------------------------------

ImageInfo g_img;
uint32_t g_rva_world = 0, g_rva_menu = 0, g_rva_loading = 0, g_rva_fade = 0;
std::atomic<DWORD> g_game_tid{0};

DWORD FindGameWindowThread() {
    struct Ctx {
        DWORD pid;
        DWORD tid;
    } ctx{GetCurrentProcessId(), 0};
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
            if (pid != c->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
            if (GetWindowTextLengthW(hwnd) <= 0) return TRUE;
            c->tid = tid;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.tid;
}


bool GameInForeground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// ---- RuntimeContext integration ------------------------------------------------------------------------------

auto& g_ctx = eldenring::RuntimeContext::Instance();
auto& g_mode = g_ctx.mode();
auto& g_player = g_ctx.player();
auto& g_world = g_ctx.world();
auto& g_combat = g_ctx.combat();
auto& g_ui = g_ctx.ui();

std::atomic<bool>& g_damage_enabled = g_mode.damage_enabled;
std::atomic<bool>& g_mc_mode = g_mode.mc_mode;
std::atomic<uint8_t>& g_game_mode = g_mode.game_mode;
std::atomic<uint64_t>& g_mode_toast_ms = g_mode.mode_toast_ms;
inline creative::GameMode CurrentGameMode() { return g_ctx.currentGameMode(); }
std::atomic<bool>& g_input_enabled = g_mode.input_enabled;
std::atomic<bool>& g_click_attack = g_mode.click_attack;
std::atomic<bool>& g_steve_enabled = g_mode.steve_enabled;
std::atomic<bool>& g_hide_native = g_mode.hide_native;
using HiddenWord = eldenring::debug::HiddenWord;
std::unordered_map<uintptr_t, HiddenWord> g_hidden_flags; // flag word addresses we cleared -> original value and the bits we cleared
std::mutex g_native_mutex; // g_hidden_flags: the Present thread and the pre-render hook both touch it
std::atomic<bool>& g_native_hide_wanted = g_ui.native_hide_wanted;
std::atomic<unsigned>& g_native_reshown = g_ui.native_reshown;
std::atomic<bool>& g_slots_changed = g_ui.slots_changed;
std::atomic<int>& g_slot_probe = g_ui.slot_probe;
std::atomic<uint32_t>& g_hide_mask1 = g_ui.hide_mask1;
std::atomic<bool>& g_no_player_hit_vfx = g_ui.no_player_hit_vfx;
std::atomic<bool>& g_fp_persist = g_mode.fp_persist;
std::atomic<bool>& g_shadow_enabled = g_mode.shadow_enabled;
std::atomic<float>& g_ground_y = g_player.ground_y;
std::atomic<bool>& g_ground_valid = g_player.ground_valid;
std::atomic<bool>& g_first_person = g_mode.first_person;
std::atomic<float>& g_eye_height = g_mode.eye_height;
std::atomic<float>& g_kb_force = g_ui.kb_force;
std::atomic<uint32_t>& g_hide_slots = g_ui.hide_slots;
std::atomic<uint32_t>& g_hide_mask2 = g_ui.hide_mask2;
float g_steve_yaw_offset = 3.14159265f; // the orientation quaternion at PhysicsModule+0x50 faces opposite to the model forward (user verified live, 180 deg)
std::atomic<bool>& g_instant_turn = g_mode.instant_turn;
std::atomic<float>& g_turn_speed = g_mode.turn_speed;
std::atomic<bool>& g_disable_vanilla_roll = g_mode.disable_vanilla_roll;
std::atomic<int>& g_roll_vk = g_mode.roll_vk;
std::atomic<bool>& g_require_victim_updating = g_mode.require_victim_updating;
std::atomic<float>& g_damage_scale = g_mode.damage_scale;
std::atomic<float>& g_damage_hp_percent = g_mode.damage_hp_percent;
std::atomic<float>& g_sweep_radius = g_mode.sweep_radius;
DamageQueue& g_queue = g_combat.damage_queue;

// Attack rules come from mc::CombatEngine; hits are applied through g_queue, so this port only exists to build the engine.
class NoCombatPort : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent&) override { return false; }
    float getMaxHealth(mc::EntityId) override { return 0.f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float) override {}
};
NoCombatPort g_no_combat_port;
mc::CombatEngine g_combat_engine{g_no_combat_port};
std::mutex& g_melee_mutex = g_combat.melee_mutex;
MeleeController& g_melee = g_combat.melee;
JumpTracker& g_jump = g_player.jump;
eldenring::sprint::SprintController& g_sprint = g_player.sprint;
std::atomic<bool>& g_sprint_active = g_player.sprint_active;
std::atomic<float>& g_sprint_fov = g_player.sprint_fov;
std::atomic<float>& g_sprint_anim = g_player.sprint_anim;
SurvivalState& g_survival = g_player.survival;
HungerModel& g_hunger = g_player.hunger;
XpModel& g_xp = g_player.xp;
float& g_hunger_heal_carry = g_player.hunger_heal_carry;
mc::ConsumableSystem& g_eating = g_player.eating;
std::atomic<float>& g_sound_volume = g_mode.sound_volume;
std::atomic<float>& g_walk_speed = g_mode.walk_speed;
std::atomic<bool>& g_on_ground = g_mode.on_ground;
std::atomic<bool>& g_bullet_fire_pending = g_mode.bullet_fire_pending;
std::atomic<bool>& g_bullet_fire_enabled = g_mode.bullet_fire_enabled;
std::atomic<bool>& g_summon_pending = g_mode.summon_pending;
std::atomic<int>& g_heal_pending = g_mode.heal_pending;
using ApplyHpFn = void*(__fastcall*)(void* data_module, int32_t hp, uint8_t flag);
ApplyHpFn g_apply_hp = nullptr;
HitFeedback& g_feedback = g_combat.feedback;
thread_local int32_t t_hp_after = -1; // victim hp right after our vfunc[7] call (game thread)
std::optional<HitTemplate>& g_template = g_combat.hit_template;
using DamageFn = uint64_t(__fastcall*)(void*, void*, void*);

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

void UpdateNativeModel(uintptr_t player, bool hide); // defined with the overlay code below
void HideSummonModels();                             // defined after WriteBytesSafe

void ReadHurt(float& hurt, float& side) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    hurt = g_feedback.hurt();
    side = g_feedback.hurtSide();
}

// ---- no stagger (experiment, config no_stagger=1) -------------------------------------------------------------------------
// Every hit the player takes goes through one of two reaction pickers (REVERSE 21). While the experiment is on they do nothing for the
// player (the output byte is cleared), so the hit still costs hit points but plays no reaction animation. Game thread only.
uintptr_t PlayerChrPtr(); // defined below

std::atomic<bool> g_no_stagger{false};
std::atomic<unsigned> g_stagger_skipped{0};

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

// ---- fall protection (fall_protect=1) ---------------------------------------------------------------------------------------
// Standing on a placed block or in our own jump tells the game the player landed from a height it measures against the real ground,
// which can be several metres below: the game then kills the player through its "kill character" function (hit points set to 0, no hit
// context, so the damage pipeline never sees it). While blocks exist or a jump of ours is recent, that kill is skipped for the player.
std::atomic<bool> g_fall_protect{true};
std::atomic<uint64_t> g_movement_layer_ms{0}; // last time one of our movement features moved the player (tick count)
std::atomic<unsigned> g_kills_skipped{0};
uintptr_t PlayerChrPtr(); // defined below
bool BlocksPresent();     // defined with the placed blocks
bool PlayerDead();        // defined below

// ---- creative-mode hooks: the fall-height hook is always installed (it only acts in creative mode, F5); the other three are log-only probes (mc_er_creativelog.txt) -------------------------------------
std::mutex g_cr_mutex;
creative::FallLogState g_cr_fall_state;
std::atomic<bool> g_cr_log{false}; // mc_er_creativelog.txt: log-only probes
float FallTimer(uintptr_t chr);    // defined with the block collision below

uintptr_t ModuleOwner(void* fall_module) {
    uint64_t owner = 0;
    return SafeCopy(reinterpret_cast<uintptr_t>(fall_module) + 8, &owner, sizeof(owner)) ? static_cast<uintptr_t>(owner) : 0;
}

bool CreativeShouldLogFall(float metres, uintptr_t caller_rva) {
    std::lock_guard<std::mutex> lock(g_cr_mutex);
    return creative::shouldLogFall(g_cr_fall_state, metres, caller_rva, GetTickCount64());
}

std::atomic<bool> g_summon_hide{false};

// ---- inventory screen ----------------------------------------------------------------------------------------------------
// While the inventory is open the game must not walk, roll, attack or turn the camera: the input master gate (0x14067B020) and the
// camera-rotation freeze test (0x140766C60) both report "blocked" (REVERSE 12). Both are optional: without them the mouse buttons and
// motion are still taken away from the game, and only the keyboard and gamepad keep moving the character.
std::atomic<bool>& g_inv_open = g_ui.inv_open;
std::atomic<float>& g_inv_mx = g_ui.inv_mx;
std::atomic<float>& g_inv_my = g_ui.inv_my;
std::atomic<int>& g_inv_vk = g_ui.inv_vk;
std::atomic<float>& g_inv_sens = g_ui.inv_sens;
std::atomic<unsigned>& g_isblocked_forced = g_ui.isblocked_forced;
std::atomic<unsigned>& g_menufreeze_forced = g_ui.menufreeze_forced;

// ---- persistent eye camera ----------------------------------------------------------------------------------------------
// ChrCam is read by more than the render copy: the effects and sound systems read its position through other functions, so a
// change that only lasts for the render copy leaves hit effects at the third-person position. Here the eye position is written
// right after the camera task has computed the frame's camera and stays there until the next camera task starts; the engine's
// own value is put back first so its camera logic never sees ours. Game thread only (the camera task runs there).
uintptr_t PlayerChrPtr(); // defined below
struct CamKeep {
    uintptr_t pos_addr{0};
    float third_person[3]{};
    bool valid{false};
};
CamKeep g_cam_keep;
std::atomic<bool> g_camstep_installed{false}; // the camera update hook (game thread, every frame) is in place
void BlocksCollisionStep(); // defined with the placed blocks below
void EnemiesBlockCollisionStep(const float player_feet[3]);
void ShadowGroundStep();    // the same
void McJumpStep();          // defined after them
void FlightStep();          // defined after them
void DriveVelocityFromKeys(uintptr_t module, bool force_zero);
void GroundSprintStep(float dt);
void VirtualSummonsStep(float dt);
RayResult CastStaticRay(const float from[3], const float disp[3]);
extern std::atomic<bool> g_flying;
std::atomic<unsigned> g_camstep_calls{0}, g_camstep_changed{0};

bool WriteBytesSafe(uintptr_t address, const void* src, size_t n) {
    if (address < 0x10000) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Summons (team 47) are drawn by us in the end: their own model is hidden by clearing the "drawn" bit of every CSModelDispEntity they own, every
// frame before the draw, the way the player's parts are hidden. mc_er_summonhide.txt turns it on. Everything is looked up afresh each frame (no
// cached pointers: a summon that disappears meanwhile is simply not enumerated any more).
std::mutex g_summon_mutex;
std::vector<eldenring::mobs::MobSnapshot> g_summon_list; // the team 47 summons seen by the last HideSummonModels (render thread), read by the HUD provider
std::vector<eldenring::summon::ArrowDraw> g_summon_arrows; // active in-flight arrows from virtual summons
float g_mob_yaw_offset = eldenring::live::summon::kDefaultMobYawOffset; // monster/wolf orientation quaternion aligns forward; default 0 (mob_yaw_offset_deg)
eldenring::summon::VirtualSummonManager g_virtual_summons;

std::set<uintptr_t> g_hidden_flag_addresses; // the disp_flags words whose drawn bit we cleared (render thread only), to put it back
std::map<uintptr_t, uint16_t> g_suppressed_sfx_slots; // the sfx slot control words whose skip bit we set, to restore on exit

void HideSummonChr(uintptr_t chr) {
    if (chr == 0) return;
    for (const uintptr_t at : collectChrDispFlagAddresses(g_reader, g_img.base, chr)) {
        uint32_t flags = 0;
        if (SafeCopy(at, &flags, sizeof(flags)) && needsHiding(flags)) {
            const uint32_t hidden = hideDrawnBit(flags);
            if (WriteBytesSafe(at, &hidden, sizeof(hidden))) {
                g_hidden_flag_addresses.insert(at);
            }
        }
    }

    // Suppress all character SFX slots (ChrConditionSfxSeSlot, ChrMultiSfxSlot, ChrSingleSfxSlot)
    // to eliminate spirit particles, auras and attached audio emitters
    for (const uintptr_t slot : collectChrSfxSlots(g_reader, g_img.base, chr)) {
        uint16_t ctrl = 0;
        const uintptr_t ctrl_addr = slot + summon::kSlotCtrlWord;
        if (SafeCopy(ctrl_addr, &ctrl, sizeof(ctrl)) && needsSfxSuppression(ctrl)) {
            g_suppressed_sfx_slots[ctrl_addr] = ctrl;
            const uint16_t suppressed = suppressSfxSlotFlags(ctrl);
            WriteBytesSafe(ctrl_addr, &suppressed, sizeof(suppressed));
        }

        // Mask visibility flag on any active particle emitter (CSFD4SfxIns) to hide existing particles
        // safely without invoking mutating intrusive destructor methods cross-thread!
        uintptr_t sfx_ins = 0;
        if (SafeCopy(slot + summon::kSlotActiveSfx, &sfx_ins, sizeof(sfx_ins)) &&
            sfx_ins >= 0x10000 && sfx_ins < 0x00007FFFFFFEFFFF && (sfx_ins & 7) == 0) {
            uint32_t sfx_flags = 0;
            const uintptr_t flags_addr = sfx_ins + summon::kSfxFlagsOffset;
            if (SafeCopy(flags_addr, &sfx_flags, sizeof(sfx_flags)) && (sfx_flags & summon::kSfxVisibleBit) != 0) {
                const uint32_t hidden_sfx = summon::hideSfxBitmask(sfx_flags);
                WriteBytesSafe(flags_addr, &hidden_sfx, sizeof(hidden_sfx));
            }
        }
    }
}

void HideSummonModels() {
    if (!g_summon_hide.load(std::memory_order_relaxed)) return;
    const bool mc_on = g_mc_mode.load(std::memory_order_relaxed);
    if (!mc_on && g_hidden_flag_addresses.empty() && g_suppressed_sfx_slots.empty()) {
        std::lock_guard<std::mutex> g(g_summon_mutex);
        g_summon_list.clear();
        return;
    }
    std::vector<EnemyInfo> list;
    std::vector<eldenring::mobs::MobSnapshot> seen;
    if (!enumerateEnemies(g_reader, g_img.base, list, 4000, /*include_dead=*/true)) { // the corpses too: the mob over a dead summon plays its death scene
        std::lock_guard<std::mutex> g(g_summon_mutex);
        g_summon_list.clear();
        return;
    }
    static unsigned logged = 0;
    for (const EnemyInfo& e : list) {
        if (e.team != eldenring::live::summon::kTeam) continue;
        {   // where it stands and which way it faces, for the figure drawn over it
            eldenring::mobs::MobSnapshot snap;
            float q[4];
            snap.id = e.chr;
            snap.hp = e.hp;
            snap.max_hp = e.max_hp;
            snap.scale = 1.0f;
            snap.white_flash = 0.0f;
            if (detail::readPhysicsPosition(g_reader, g_img.base, e.chr, snap.feet) && detail::readPhysicsOrientation(g_reader, g_img.base, e.chr, q)) {
                snap.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]) + g_mob_yaw_offset;
                seen.push_back(snap);
            }
        }
        if (mc_on) {
            HideSummonChr(e.chr);
        } else {
            // Restore model display flags when MC mode is turned off
            for (const uintptr_t at : collectChrDispFlagAddresses(g_reader, g_img.base, e.chr)) {
                uint32_t flags = 0;
                if (!SafeCopy(at, &flags, sizeof(flags))) continue;
                if (g_hidden_flag_addresses.count(at) != 0 && !needsHiding(flags)) {
                    const uint32_t shown = showDrawnBit(flags);
                    if (WriteBytesSafe(at, &shown, sizeof(shown)) && ++logged <= 24) Log("summon hide: MC mode off, restored flags at %p 0x%X -> 0x%X", reinterpret_cast<void*>(at), flags, shown);
                }
            }
            // Restore Sfx slot control words
            for (const uintptr_t slot : collectChrSfxSlots(g_reader, g_img.base, e.chr)) {
                const uintptr_t ctrl_addr = slot + summon::kSlotCtrlWord;
                const auto it = g_suppressed_sfx_slots.find(ctrl_addr);
                if (it != g_suppressed_sfx_slots.end()) {
                    const uint16_t orig_ctrl = it->second;
                    WriteBytesSafe(ctrl_addr, &orig_ctrl, sizeof(orig_ctrl));
                }
            }
        }
    }

    {   // Diagnostic: what a summon's hit points do when it is hit and when it dies, and how it leaves (does it stay at 0 hp for a while, or just vanish?)
        static std::map<uintptr_t, int> last_hp;
        static unsigned traced = 0;
        for (const auto& snap : seen) {
            const auto it = last_hp.find(snap.id);
            if (it == last_hp.end()) {
                if (++traced <= 80) Log("mob: chr=%p appeared, hp %d/%d", reinterpret_cast<void*>(snap.id), snap.hp, snap.max_hp);
            } else if (it->second != snap.hp && ++traced <= 80) {
                Log("mob: chr=%p hp %d -> %d", reinterpret_cast<void*>(snap.id), it->second, snap.hp);
            }
            last_hp[snap.id] = snap.hp;
        }
        for (auto it = last_hp.begin(); it != last_hp.end();) {
            bool still = false;
            for (const auto& snap : seen) still = still || snap.id == it->first;
            if (still) {
                ++it;
                continue;
            }
            if (++traced <= 80) Log("mob: chr=%p gone, its last hp was %d", reinterpret_cast<void*>(it->first), it->second);
            it = last_hp.erase(it);
        }
    }
    std::vector<eldenring::summon::ArrowDraw> virt_arrows;
    if (!mc_on || PlayerDead()) {
        g_hidden_flag_addresses.clear(); // the ones left belong to summons that are gone: nothing to put back
    } else {
        const auto virt_snaps = g_virtual_summons.generateSnapshots();
        seen.insert(seen.end(), virt_snaps.begin(), virt_snaps.end());
        virt_arrows = g_virtual_summons.getActiveArrows();
    }
    std::lock_guard<std::mutex> g(g_summon_mutex);
    g_summon_list = mc_on ? seen : std::vector<eldenring::mobs::MobSnapshot>{};
    g_summon_arrows = mc_on ? virt_arrows : std::vector<eldenring::summon::ArrowDraw>{};
}

void VirtualSummonsStep(float dt) {
    const bool mc_on = g_mc_mode.load(std::memory_order_relaxed);
    if (!mc_on || PlayerDead()) {
        g_virtual_summons.dismissAll();
        return;
    }
    if (g_virtual_summons.activeCount() == 0) return;

    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return;

    float p_pos[3], p_q[4];
    if (!detail::readPhysicsPosition(g_reader, g_img.base, player, p_pos) ||
        !detail::readPhysicsOrientation(g_reader, g_img.base, player, p_q)) {
        return;
    }

    eldenring::summon::PlayerPose pp;
    pp.pos[0] = p_pos[0];
    pp.pos[1] = p_pos[1];
    pp.pos[2] = p_pos[2];
    pp.yaw = eldenring::render::yawFromQuat(p_q[0], p_q[1], p_q[2], p_q[3]);

    static std::once_flag samplers_init;
    std::call_once(samplers_init, [] {
        g_virtual_summons.setLineOfSightSampler([](const float from[3], const float to[3]) -> bool {
            float disp[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
            const RayResult hit = CastStaticRay(from, disp);
            if (hit.ok && hit.hit && hit.fraction < 0.95f) return false;
            return true;
        });
        g_virtual_summons.setWorldRaycastSampler([](const float from[3], const float to[3], float hit_pos[3]) -> bool {
            float disp[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
            const RayResult hit = CastStaticRay(from, disp);
            if (hit.ok && hit.hit) {
                hit_pos[0] = hit.pos[0];
                hit_pos[1] = hit.pos[1];
                hit_pos[2] = hit.pos[2];
                return true;
            }
            return false;
        });
    });

    std::vector<EnemyInfo> enemies;
    enumerateEnemies(g_reader, g_img.base, enemies, 512);

    std::vector<eldenring::summon::EnemyTarget> enemy_targets;
    for (const EnemyInfo& e : enemies) {
        if (e.team != eldenring::live::summon::kTeam && e.hp > 0 && e.chr != player && e.hostile) {
            eldenring::summon::EnemyTarget et;
            et.chr_ins = e.chr;
            et.hp = e.hp;
            et.max_hp = e.max_hp;
            et.is_hostile = true;
            detail::readPhysicsPosition(g_reader, g_img.base, e.chr, et.pos);
            enemy_targets.push_back(et);
        }
    }

    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    g_virtual_summons.update(dt, pp, enemy_targets);

    auto intents = g_virtual_summons.drainDamageIntents();
    for (const auto& in : intents) {
        g_queue.enqueue(in.target_chr_ins, static_cast<int>(in.damage_amount), NowTick());
        eraudio::Play("entity.arrow.hit", 1.0f);
    }

    auto proj_intents = g_virtual_summons.drainProjectileIntents();
    for (const auto& pi : proj_intents) {
        eraudio::Play("entity.arrow.shoot", 1.0f);
        erov::SpawnFx(erov::FxKind::Crit, pi.origin, 2);
        Log("virtual ranged summon: fired projectile at target %llX (damage=%.1f)",
            static_cast<unsigned long long>(pi.target_chr_ins), pi.damage);
    }

    auto exp_events = g_virtual_summons.drainExplosionEvents();
    for (const auto& ev : exp_events) {
        eraudio::Play("entity.generic.explode", 1.0f);
        Log("virtual creeper: DETONATION at (%.2f %.2f %.2f)!", ev.pos[0], ev.pos[1], ev.pos[2]);
        eldenring::world::VoxelWorldRuntime::Instance().enqueueTntIntent(ev.pos, ev.radius, ev.damage);
    }

    const auto active_arrows = g_virtual_summons.getActiveArrows();
    for (const auto& arr : active_arrows) {
        erov::SpawnFx(erov::FxKind::Crit, arr.pos, 1);
    }
    {
        std::lock_guard<std::mutex> g(g_summon_mutex);
        g_summon_arrows = active_arrows;
    }
}

bool ChrCamPosAddress(uintptr_t& addr) {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    uint64_t cam = 0;
    if (world == 0 || !SafeCopy(world + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) || cam == 0) return false;
    addr = static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30;
    return true;
}

void RestoreCamKeep() {
    if (!g_cam_keep.valid) return;
    WriteBytesSafe(g_cam_keep.pos_addr, g_cam_keep.third_person, sizeof(g_cam_keep.third_person));
    g_cam_keep.valid = false;
}

void AfterCameraStep(bool have_before, uintptr_t addr, const float before[3]) {
    const unsigned call = ++g_camstep_calls;
    float now[3];
    if (!have_before || !SafeCopy(addr, now, sizeof(now))) return;
    const bool changed = std::fabs(now[0] - before[0]) + std::fabs(now[1] - before[1]) + std::fabs(now[2] - before[2]) > 1e-5f;
    if (changed) ++g_camstep_changed;
    float eye[3] = {};
    bool wrote = false;
    if (g_mode.modOwns(mc::ControlDomain::Camera) && g_fp_persist.load(std::memory_order_relaxed)) {
        const uintptr_t player = PlayerChrPtr();
        float feet[3];
        if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) {
            firstPersonEye(feet, g_eye_height.load(), eye);
            if (WriteBytesSafe(addr, eye, sizeof(eye))) {
                g_cam_keep.pos_addr = addr;
                std::memcpy(g_cam_keep.third_person, now, sizeof(now));
                g_cam_keep.valid = true;
                wrote = true;
            }
        }
    }
    if (call <= 8 || call % 1200 == 0) {
        Log("camstep: call %u changed=%d (changed so far %u) engine pos=(%.2f %.2f %.2f)%s", call, changed ? 1 : 0, g_camstep_changed.load(), now[0], now[1], now[2],
            wrote ? " -> eye written" : "");
    }
}

void RunBulletFire();

// ---- camera write watch (diagnostic, opt-in: mc_er_camwatch.txt) ----------------------------------------------------------
using eldenring::debug::CamWatchThread;

// ---- player hit effect (blood) -----------------------------------------------------------------------------------------
// 0x140450120 spawns the hit particles; its first gate is IsMainPlayer(victim), so it only runs for hits the player takes.
// For those it returns "nothing spawned" (0) without calling the game, when the experiment is on.
std::atomic<unsigned> g_hitvfx_skipped{0};

// ---- line of sight (delegated to CombatService) -------------------------------------------------------------
std::atomic<bool> g_los_enabled{false};
std::atomic<unsigned> g_los_blocked{0};

RayResult CastStaticRay(const float from[3], const float disp[3]) {
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

bool PlayerDead();

int32_t HpOf(uintptr_t chr) {
    Vitals v;
    return readVitals(g_reader, g_img.base, chr, v) ? v.hp : -1;
}

std::atomic<bool> g_pdc_hooked{false};
std::atomic<bool> g_log_player_hits{false}; // mc_er_hitlog.txt: log the HitContext of every hit the player takes (read only)

// Read-only: one line per hit the player takes, with the HitContext fields the reverser needs (blood effect, flinch, knockback).
uintptr_t DataModuleOfChr(uintptr_t chr);

// Read-only (REVERSE 37, to check the reverser's claim): the four homogeneous vectors the hit context is said to keep at +0x190 (impact point),
// +0x1A0 (attack source point), +0x1B0 (attack direction) and +0x1C0 (hit normal / knockback direction), next to the positions we can read
void LogHitGeometry(const char* tag, void* attacker, uintptr_t player, const uint8_t* ctx) {
    eldenring::debug::LogHitGeometry(tag, attacker, player, ctx, g_reader, g_img.base);
}

void LogPlayerHit(void* module, void* attacker, const uint8_t* ctx, uint8_t blocked_flag) {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::LogPlayerHit(module, attacker, ctx, blocked_flag, g_reader, g_img.base, world);
}

// Read-only (mc_er_chrscan.txt, after F2): the classes behind the pointers of a summoned character, and inside the model-like ones where a dword
// equal to the live "drawn" display flags (0x000100A1) sits. Finds where a monster model keeps what the player's CSModelDispEntity keeps.
unsigned g_chrscan_count = 0; // reset by every F2
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

// Gives hit points back through the game's own SetHP (bars and HUD follow). Game thread only.
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

// Sets character HP through the game's SetHP / ApplyHpChange function (0x140437450), or direct memory write.
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

std::atomic<bool>& g_shield_blocking = g_mode.shield_blocking;
std::atomic<bool>& g_shield_offhand = g_mode.shield_offhand; // the shield being raised is the offhand's (the left arm goes up), else the main hand's

// What the raised shield decides about a hit on the player. The shield guards where the player LOOKS: the horizontal part of the camera's forward
// vector (live data 2026-10-11: ER's body orientation only turns when the character moves and a hit reaction can even turn it, so against it the
// attacker read as "behind" in every hit although the camera was on the attacker). The body yaw is kept for the log and as the fallback when the
// camera cannot be read. `attacker_chr` 0 means there is none (never blocked).
using eldenring::combat::ShieldVerdict;

ShieldVerdict EvaluateShield(uintptr_t player, uintptr_t attacker_chr) {
    return eldenring::combat::CombatService::Instance().evaluateShield(player, attacker_chr);
}

// Natural hits on the player: absorption soaks damage up, then a totem keeps a killing hit at 1 HP. Returns true when a totem was used.
bool PlayerHitSurvival(void* module, void* attacker, uint8_t* ctx, uint8_t blocked, uint32_t& popped_heal_target) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    auto& r_ctx = eldenring::RuntimeContext::Instance();
    r_ctx.mode().shield_blocking.store(g_shield_blocking.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return eldenring::combat::CombatService::Instance().playerHitSurvival(
        module, attacker, ctx, blocked, popped_heal_target,
        g_melee, g_survival, g_feedback, g_hunger, r_ctx);
}

// Projectiles that hit somebody other than the player (mc_er_bulletlog.txt: log; mc_er_arrowdmg.txt: replace the damage of the player's own bolts with the
// MC bolt damage). The first run only logs: which of attacker / ctx+0x1D8 is the shooter for a projectile was not known (REVERSE 35.5).
std::atomic<bool> g_proj_log{false};
std::atomic<bool> g_arrow_damage{false};
std::atomic<int> g_proj_log_left{80};
std::atomic<bool>& g_bow_offhand = g_mode.bow_offhand;
std::atomic<float>& g_bow_power = g_mode.bow_power;
std::atomic<float>& g_bow_ticks = g_mode.bow_ticks;

void RunBulletFire(); // forward declaration for CameraStepDetour

void HandleProjectileHit(void* module, void* attacker, uint8_t* ctx) {
    eldenring::combat::CombatService::Instance().handleProjectileHit(
        module, attacker, ctx, g_feedback, eldenring::RuntimeContext::Instance());
}

std::atomic<uint64_t> g_guard_sound_ms{0};
std::atomic<bool> g_knockdown_fix{true};

void DrainOnce(uintptr_t updating_data_module) {
    eldenring::combat::CombatService::Instance().drainDamageQueue(
        g_queue, g_template ? &*g_template : nullptr, updating_data_module, NowTick(),
        g_melee.inventory(), g_feedback, g_xp, eldenring::RuntimeContext::Instance());
}

void RunSummonExperiment() {
    // Deprecated: CSBuddyMan spirit ash pipeline is completely replaced by client-managed VirtualSummonManager.
    // Intentionally no-op to eliminate all native Dismiss crashing.
}

bool QueueBulletFire(float mc_damage);
void AimCalTick();

void EnqueueNearestHostile() {
    eldenring::combat::CombatService::Instance().enqueueNearestHostile(g_queue, NowTick());
}

// MC left click: the hostile enemy under the crosshair, in melee reach.
bool ClickAttack(float charged) {
    return eldenring::combat::CombatService::Instance().clickAttack(
        charged, g_melee, g_combat_engine, g_queue, eldenring::RuntimeContext::Instance());
}

// Should the native character ignore the mouse buttons right now? Only while the MC HUD is on and the game shows no UI.
bool WantSuppress() {
#if ENABLE_CS2_VIEWMODEL
    if (!g_mc_mode.load() && !eldenring::RuntimeContext::Instance().mode().cs_mode.load()) return false;
#else
    if (!g_mc_mode.load()) return false;
#endif
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

bool FileExists(const std::string& path); // defined below
bool ReadFileAll(const std::string& path, std::vector<uint8_t>& out);

// Diagnostic: +0x70 (position), +0x80 and +0x120 of the player's physics module, twice a second while moving. Answers whether
// +0x80 is the previous position (fromsoftware-rs: last_update_position) or a velocity (reverser 2026-10-09).
void LogPhysicsVectors() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::LogPhysicsVectors(g_reader, g_img.base, world);
}

void LogPlayerPoseForBuddy() {
    eldenring::debug::LogPlayerPoseForBuddy(g_reader, g_img.base, PlayerChrPtr());
}

using eldenring::debug::BuddyThread;

// Diagnostic: the player's ground-contact bytes, one log line each time they change (jump / fall / roll / swim).
void LogGroundBytesOnChange() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::LogGroundBytesOnChange(g_reader, g_img.base, world);
}

// F11: write down, right now, the state of every part slot of the player's native model, so a model that is visible in
// first person can be compared with what we believe is hidden.
void SnapshotNativeParts() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    eldenring::debug::SnapshotNativeParts(g_reader, g_img.base, world, g_ctx, g_hidden_flags, g_native_mutex);
}

// Back to hiding every part slot: a probe left on a single slot would otherwise keep the rest of the model visible.
void ResetSlotProbe(int& cursor) {
    eldenring::debug::ResetSlotProbe(cursor, g_hide_slots, g_slot_probe, g_slots_changed, [](const char* s) { Log("%s", s); });
}

// F9: hide one part slot at a time (to find out which body part a slot is), then all of them again.
void CycleHiddenSlot(int& cursor) {
    const uintptr_t player = PlayerChrPtr();
    eldenring::debug::CycleHiddenSlot(cursor, player, g_reader, g_img.base, g_hide_slots, g_slot_probe, g_slots_changed);
}

// ---- placed blocks (phase A: our own grid, no physics bodies in the game) ----------------------------------------------------
// Right click with a block in hand places it, left click on a placed block breaks it. The game's physics knows nothing about the
// blocks: the player is kept out of them by writing the physics position (soft collision); enemies and arrows ignore them.
blocks::BlockGrid& g_blocks = g_world.block_grid;
eldenring::tnt::ErTntManager& g_tnt_mgr = g_world.tnt_mgr;
std::mutex& g_blocks_mutex = g_world.blocks_mutex;
std::atomic<bool>& g_blocks_enabled = g_mode.blocks_enabled;
std::atomic<float>& g_block_walk_speed = g_mode.block_walk_speed;
std::atomic<float>& g_sneak_speed = g_mode.sneak_speed;
std::atomic<float>& g_sprint_speed = g_mode.sprint_speed;
std::atomic<int>& g_sneak_vk = g_mode.sneak_vk;
std::atomic<bool>& g_sneak_active = g_mode.sneak_active;
std::atomic<bool> g_sneak_clamped_x{false};
std::atomic<bool> g_sneak_clamped_z{false};
std::atomic<bool>& g_block_drive = g_mode.block_drive;
std::atomic<bool>& g_block_anchor = g_mode.block_anchor;
std::atomic<bool>& g_block_ground = g_mode.block_ground;
std::atomic<bool>& g_block_collision = g_mode.block_collision;
std::atomic<bool>& g_enemy_block_collision = g_mode.enemy_block_collision;
std::atomic<bool>& g_block_supported = g_mode.block_supported;
std::atomic<float>& g_reach = g_mode.reach;
unsigned& g_blocks_sent_version = g_world.blocks_sent_version;

bool BlocksPresent() {
    std::lock_guard<std::mutex> g(g_blocks_mutex);
    return g_blocks.count() != 0;
}

static size_t g_tnt_prev_active_count = 0;
static int g_prev_mining_stage = -1;
static blocks::Cell g_prev_mining_cell{};

void SendBlockMesh(const std::optional<blocks::MiningOverlay>& mining = std::nullopt) {
    std::lock_guard<std::mutex> g(g_blocks_mutex);
    const size_t tnt_count = g_tnt_mgr.activeCount();
    const int stage = mining.has_value() ? mining->stage : -1;
    const blocks::Cell cell = mining.has_value() ? mining->cell : blocks::Cell{};
    if (g_blocks_sent_version == g_blocks.version() && tnt_count == 0 && g_tnt_prev_active_count == 0 &&
        stage == g_prev_mining_stage && cell == g_prev_mining_cell) return;
    g_blocks_sent_version = g_blocks.version();
    g_tnt_prev_active_count = tnt_count;
    g_prev_mining_stage = stage;
    g_prev_mining_cell = cell;
    erov::SetBlockMesh(blocks::buildBlockMesh(g_blocks, g_tnt_mgr.activePositions(), mining));
}


struct BlockTarget {
    bool have{false};
    bool on_block{false};        // the nearest surface is a placed block (else the game's terrain)
    blocks::Cell cell;           // the placed block, when on_block
    blocks::Cell place;          // where a new block would go
};

// The aim ray: from the player's eye along the camera's forward vector.
bool AimRay(float eye[3], float dir[3]) {
    const uintptr_t player = PlayerChrPtr();
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    CameraPose cam;
    float feet[3];
    if (player == 0 || world == 0 || !readCamera(g_reader, g_img.base, world, cam) || !detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) return false;
    eye[0] = feet[0];
    eye[1] = feet[1] + g_eye_height.load();
    eye[2] = feet[2];
    for (int i = 0; i < 3; ++i) dir[i] = cam.forward[i];
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 0.5f)) return false;
    for (int i = 0; i < 3; ++i) dir[i] /= len;
    return true;
}

BlockTarget FindBlockTarget() {
    BlockTarget t;
    float eye[3], dir[3];
    if (!AimRay(eye, dir)) return t;
    const float reach = g_reach.load();
    std::optional<blocks::GridHit> grid_hit;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        grid_hit = blocks::raycastGrid(g_blocks, eye, dir, reach);
    }
    RayResult world_hit;
    if (g_los_enabled.load()) {
        const float disp[3] = {dir[0] * reach, dir[1] * reach, dir[2] * reach};
        world_hit = CastStaticRay(eye, disp);
    }
    const bool terrain = world_hit.ok && world_hit.hit;
    const float terrain_dist = terrain ? world_hit.fraction * reach : 1e9f;
    if (grid_hit && grid_hit->distance <= terrain_dist + 0.05f) {
        t.have = true;
        t.on_block = true;
        t.cell = grid_hit->cell;
        t.place = blocks::placementCellForBlockHit(*grid_hit);
    } else if (terrain) {
        t.have = true;
        t.place = blocks::placementCellForWorldHit(world_hit.pos, world_hit.normal);
    }
    return t;
}

const char* BlockSound(mc::BlockId id) {
    switch (id) {
        case mc::BlockId::Stone: return "block.stone.step";
        case mc::BlockId::Dirt: return "block.gravel.step";
        default: return "block.grass.step";
    }
}

// ---- summoning (F2, or a right click with the zombie spawn egg) ---------------------------------------------------------------------
// One summon request for the game thread (ClampDetour -> RunSummonExperiment). The entities seen before are remembered so that the ones that
// appear afterwards can be reported (and scanned, mc_er_chrscan.txt).
struct SummonWatch {
    std::vector<uintptr_t> baseline;
    uint64_t t0_ms{0};
    int reports_left{0};
};
SummonWatch g_summon_watch;
std::atomic<uint64_t> g_last_summon_ms{0};
constexpr uint64_t kSummonCooldownMs = 200; // Fast and responsive virtual summon (no FromSoftware engine locks)

// Returns false when capacity is full or player pose unreadable. Completely decoupled from CSBuddyMan.
bool QueueSummon(const char* source, const char* requested_mob = nullptr) {
    const uint64_t now = GetTickCount64();
    if (now - g_last_summon_ms.load(std::memory_order_relaxed) < kSummonCooldownMs) return false;
    g_last_summon_ms.store(now);

    const uintptr_t player = PlayerChrPtr();
    if (player == 0) {
        Log("%s: refused (no player entity)", source);
        return false;
    }
    float pos[3], q[4];
    if (!detail::readPhysicsPosition(g_reader, g_img.base, player, pos) ||
        !detail::readPhysicsOrientation(g_reader, g_img.base, player, q)) {
        Log("%s: refused (player position/orientation not readable)", source);
        return false;
    }

    eldenring::summon::PlayerPose p;
    p.pos[0] = pos[0];
    p.pos[1] = pos[1];
    p.pos[2] = pos[2];
    p.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]);

    std::string model_type = (requested_mob != nullptr && requested_mob[0] != '\0') ? requested_mob : "creeper";
    std::vector<uint8_t> summon_txt;
    if (requested_mob == nullptr && ReadFileAll(g_game_dir + "mc_er_summon.txt", summon_txt) && !summon_txt.empty()) {
        std::string content(reinterpret_cast<const char*>(summon_txt.data()), summon_txt.size());
        for (char& c : content) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (content.find("creeper") != std::string::npos) model_type = "creeper";
        else if (content.find("skeleton") != std::string::npos) model_type = "skeleton";
        else if (content.find("iron_golem") != std::string::npos || content.find("golem") != std::string::npos) model_type = "iron_golem";
        else if (content.find("wolf") != std::string::npos) model_type = "wolf";
        else if (content.find("zombie") != std::string::npos) model_type = "zombie";
    }

    uint64_t id = g_virtual_summons.spawnMob(model_type, p, 2.5f);
    Log("%s: virtual summon created (type=%s, id=%llu, active=%zu) at (%.2f, %.2f, %.2f) yaw=%.2f",
        source, model_type.c_str(), id, g_virtual_summons.activeCount(), pos[0], pos[1], pos[2], p.yaw);
    return true;
}

// Right click with any spawn egg in hand: one summon corresponding to the egg; in survival the egg is used up.
bool TryUseSpawnEgg() {
    mc::ItemId held = mc::ItemId::None;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        held = g_melee.heldItem();
        if (!mc::isSpawnEgg(held) || g_melee.countAt(g_melee.selectedSlot()) == 0) return false;
    }
    const std::string_view mob_type = mc::spawnEggToMobType(held);
    const std::string mob_str = mob_type.empty() ? "zombie" : std::string(mob_type);
    if (!QueueSummon("spawn egg", mob_str.c_str())) return true; // refused (too soon): the click is still the egg's
    std::lock_guard<std::mutex> g(g_melee_mutex);
    if (creative::consumesItems(CurrentGameMode())) g_melee.consumeAt(g_melee.selectedSlot());
    g_melee.startSwing();
    return true;
}

// Right click on a placed TNT block ignites it.
bool TryIgniteTnt() {
    if (!g_blocks_enabled.load()) return false;
    const BlockTarget t = FindBlockTarget();
    if (!t.have || !t.on_block) return false;

    mc::BlockId target_block = mc::BlockId::Air;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        target_block = g_blocks.get(t.cell);
    }
    if (target_block != mc::BlockId::Tnt) return false;

    mc::ItemId held = mc::ItemId::None;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        held = g_melee.heldItem();
    }
    // Only FlintAndSteel ignites TNT on right click (Minecraft rule)
    if (!eldenring::tnt::shouldIgniteTntOnRightClick(held, target_block)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (!g_tnt_mgr.tryIgniteBlock(g_blocks, t.cell)) return false;
    }
    SendBlockMesh();
    eraudio::Play("entity.tnt.primed", 1.0f);
    Log("tnt: ignited TNT at (%d %d %d)", t.cell.x, t.cell.y, t.cell.z);
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        g_melee.startSwing();
    }
    return true;
}

// Right click with Flint and Steel near a summoned Creeper manually ignites it.
bool TryIgniteCreeper() {
    mc::ItemId held = mc::ItemId::None;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        held = g_melee.heldItem();
    }
    if (held != mc::ItemId::FlintAndSteel) return false;

    std::vector<eldenring::mobs::MobSnapshot> summons;
    {
        std::lock_guard<std::mutex> g(g_summon_mutex);
        summons = g_summon_list;
    }
    float player_feet[3]{};
    if (!detail::readPhysicsPosition(g_reader, g_img.base, PlayerChrPtr(), player_feet)) return false;

    for (const auto& s : summons) {
        const float dx = s.feet[0] - player_feet[0];
        const float dy = s.feet[1] - player_feet[1];
        const float dz = s.feet[2] - player_feet[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist <= 4.5f) {
            const bool ignited = g_virtual_summons.igniteCreeper(s.id);
            if (ignited) {
                eraudio::Play("entity.tnt.primed", 1.0f);
                Log("creeper: manually ignited creeper %p with flint and steel!", reinterpret_cast<void*>(s.id));
                std::lock_guard<std::mutex> g(g_melee_mutex);
                g_melee.startSwing();
                return true;
            }
        }
    }
    return false;
}

// Right click with a block in hand. Returns true when the click was used (placed, or refused for a reason the player can see).
bool TryPlaceBlock() {
    if (!g_blocks_enabled.load()) return false;
    mc::ItemId held;
    int slot;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        held = g_melee.heldItem();
        slot = g_melee.selectedSlot();
        if (g_melee.countAt(slot) == 0) return false;
    }
    const mc::BlockId id = blocks::blockForItem(held);
    if (id == mc::BlockId::Air) return false;
    const BlockTarget t = FindBlockTarget();
    if (!t.have) return true;
    const uintptr_t player = PlayerChrPtr();
    float feet[3];
    if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet) && blocks::cellTouchesPlayer(t.place, feet)) return true; // not inside yourself
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (!g_blocks.place(t.place, id)) return true;
    }
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        if (creative::consumesItems(CurrentGameMode())) g_melee.consumeAt(slot); // creative: blocks are not used up
        g_melee.startSwing();
    }
    SendBlockMesh();
    eraudio::Play(BlockSound(id), 0.8f);
    Log("blocks: placed %d at (%d %d %d)", static_cast<int>(id), t.place.x, t.place.y, t.place.z);
    return true;
}

static eldenring::mining::MiningController g_mining;

// Progressive block mining: called every tick in KeyThread.
// When left mouse is held on a placed block within reach, accumulates mining progress,
// updates crack overlay mesh, plays periodic hit sounds, and finally breaks the block.
// Returns true when the left click is consumed by mining (so it does not swing as an air attack).
bool StepMining(float dt, bool left_held) {
    if (!g_blocks_enabled.load()) {
        g_mining.reset();
        SendBlockMesh(std::nullopt);
        return false;
    }
    const BlockTarget t = FindBlockTarget();
    mc::BlockId id = mc::BlockId::Air;
    if (t.have && t.on_block) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        id = g_blocks.get(t.cell);
    }

    mc::ItemId held = mc::ItemId::None;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        held = g_melee.heldItem();
    }
    const bool is_creative = (CurrentGameMode() == creative::GameMode::Creative);


    g_mining.update(dt, left_held, t.have && t.on_block, t.cell, id, held, is_creative);
    const eldenring::mining::MiningState& s = g_mining.state();

    if (s.is_mining) {
        if (s.hit_sound) {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            g_melee.startSwing();
            eraudio::Play(BlockSound(id), 0.6f);
        }
        SendBlockMesh(blocks::MiningOverlay{s.target_cell, s.stage});
        return true;
    }

    if (s.broken) {
        bool harvested = false;
        {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            g_blocks.remove(s.target_cell);
        }
        if (eldenring::mining::MiningController::canHarvest(id, held, is_creative)) {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            g_melee.inventory().add({blocks::itemForBlock(id), 1});
            harvested = true;
        }
        SendBlockMesh(std::nullopt);
        eraudio::Play(BlockSound(id), 0.95f);
        Log("blocks: broke %d at (%d %d %d) with tool %d (harvested=%d)",
            static_cast<int>(id), s.target_cell.x, s.target_cell.y, s.target_cell.z,
            static_cast<int>(held), harvested ? 1 : 0);
        return true;
    }

    SendBlockMesh(std::nullopt);
    return false;
}


// The game keeps the horizontal velocity it had in the air (+0x120, constant for seconds in the log) and moves the player with it whatever
// key is down. While the player is held in the air state by us (standing on a block, or in our own jump) the velocity is written here: from
// WASD and the camera, or zero.
void DriveVelocityFromKeys(uintptr_t module, bool force_zero) {
    eldenring::movement::PlayerLocomotion::Instance().driveVelocity(module, force_zero, g_ctx);
}

uintptr_t FallModuleOf(uintptr_t chr) {
    return eldenring::movement::PlayerLocomotion::Instance().fallModuleOf(chr);
}

uintptr_t ActionFlagModuleOf(uintptr_t chr) {
    return eldenring::movement::PlayerLocomotion::Instance().actionFlagModuleOf(chr);
}

void UnlockAirMovement(uintptr_t chr) {
    eldenring::movement::PlayerLocomotion::Instance().unlockAirMovement(chr);
}

float FallTimer(uintptr_t chr) {
    return eldenring::movement::PlayerLocomotion::Instance().fallTimer(chr);
}

std::atomic<bool> g_fall_reset{true};
std::atomic<float> g_fall_hold{1.0f};
std::atomic<float> g_pre_fall_t{0.f};

void WriteFallTimer(uintptr_t chr, float value) {
    eldenring::movement::PlayerLocomotion::Instance().writeFallTimer(chr, value);
    g_pre_fall_t.store(eldenring::movement::PlayerLocomotion::Instance().preFallTime(), std::memory_order_relaxed);
}

void ResetFallTimer(uintptr_t chr) {
    eldenring::movement::PlayerLocomotion::Instance().resetFallTimer(chr);
}

void LandFallTimer(uintptr_t chr) {
    eldenring::movement::PlayerLocomotion::Instance().landFallTimer(chr);
}

// Keeps the player out of the blocks. Runs on the game thread, at the start of every camera update (the game's own per-frame hook), so
// the physics position is never written while the game is using it and the camera sees the corrected position.
// The ground under the player for the blob shadow: a ray straight down (game thread, like every other cast) and the placed blocks' tops.
// Without a ground the overlay falls back to the feet's own height, which is right whenever the player stands.
void ShadowGroundStep() {
    eldenring::world::VoxelWorldRuntime::Instance().stepShadowGround(g_ctx);
}

void BlocksCollisionStep() {
    eldenring::world::VoxelWorldRuntime::Instance().stepPlayerBlockCollision(g_ctx);
}

std::atomic<float> g_sprint_boost{2.5f}; // mc_er_steve.txt: sprint_boost=<m/s> (extra speed boost applied while sprinting on ground, default 2.5 m/s)

void GroundSprintStep(float dt) {
    eldenring::movement::PlayerLocomotion::Instance().stepGroundSprint(dt, g_ctx);
}

void ProcessTntExplosionsOnGameThread(const std::vector<EnemyInfo>& enemies) {
    eldenring::world::VoxelWorldRuntime::Instance().processTntExplosions(enemies, g_ctx);
}

void EnemiesBlockCollisionStep(const float player_feet[3]) {
    const bool mc_on = g_mc_mode.load(std::memory_order_relaxed);
#if ENABLE_CS2_VIEWMODEL
    const bool cs_on = eldenring::RuntimeContext::Instance().mode().cs_mode.load(std::memory_order_relaxed);
#else
    const bool cs_on = false;
#endif
    if (!mc_on && !cs_on) return;

    std::vector<EnemyInfo> enemies;
    if (!enumerateEnemies(g_reader, g_img.base, enemies, 512)) {
        EntityManager::instance().clearCache();
        return;
    }

    // The CS hitscan reads this cache on the input thread.
    EntityManager::instance().updateCacheFromGameThread(enemies);

    if (!mc_on) return;

    // Process queued TNT explosion damage intents on game thread!
    ProcessTntExplosionsOnGameThread(enemies);

    eldenring::world::VoxelWorldRuntime::Instance().stepEnemyBlockCollision(player_feet, g_ctx);
}

void TriggerTntExplosionEffectsAndDamage(const float center[3]) {
    eldenring::world::VoxelWorldRuntime::Instance().triggerTntExplosion(center, g_ctx);
}

// Called every key-thread tick: forgets the blocks when the game loads another map (the world coordinates are not the same any more),
// and keeps the player out of the blocks.
void BlocksTick() {
    static bool prev_loading = false;
    static unsigned pushes = 0;
    LoadingState ls;
    if (readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls)) {
        if (ls.screen_loading && !prev_loading) {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            if (g_blocks.count() != 0) Log("blocks: loading screen, %zu blocks forgotten", g_blocks.count());
            g_blocks.clear();
            g_tnt_mgr.clear();
            g_virtual_summons.dismissAll();
        }
        prev_loading = ls.screen_loading;
    }

    // Step active primed TNT
    static uint64_t last_tnt_ms = 0;
    const uint64_t now_ms = GetTickCount64();
    if (last_tnt_ms == 0) last_tnt_ms = now_ms;
    const float dt = std::clamp(static_cast<float>(now_ms - last_tnt_ms) / 1000.f, 0.001f, 0.1f);
    last_tnt_ms = now_ms;

    std::vector<eldenring::tnt::ErExplosionResult> explosions;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_tnt_mgr.activeCount() > 0) {
            explosions = g_tnt_mgr.step(dt, g_blocks);
        }
    }

    // Trigger effects and sounds outside of g_blocks_mutex to eliminate lock recursion
    for (const auto& exp : explosions) {
        TriggerTntExplosionEffectsAndDamage(exp.center);
    }

    SendBlockMesh();
    if (!g_camstep_installed.load()) BlocksCollisionStep(); // without the camera hook the collision runs here (less smooth)
}

// ---- MC jump (mc_jump=1) ------------------------------------------------------------------------------------------------------
// The game's jump has a 0.7-0.8 s wind-up and is low, and its vertical speed cannot be written (+0x124 is only a read-out).
// So the whole arc is ours: the jump key is hidden from the game (DirectInput), on the press (on the ground or on a block)
// the height is integrated using Minecraft kinematics (MinecraftJumpController: 8.4 m/s initial, 32 m/s^2 gravity, 0.98 drag, ~1.25m peak)
// and written into the physics position every frame. Horizontal velocity carries momentum with sprint boost (1.15x) and air steering authority.
// The landing is the game's ground (a static ray down) or the top of a placed block. Game thread (camera update).
std::atomic<bool> g_mc_jump{false};
std::atomic<int> g_mc_jump_vk{VK_SPACE};
std::atomic<float> g_mc_jump_speed{eldenring::jump::kMcJumpInitialVelocity};
std::atomic<float> g_mc_gravity{eldenring::jump::kMcJumpGravity};

void McJumpStep() {
    static LARGE_INTEGER last_qpc{};
    LARGE_INTEGER now_qpc, freq;
    QueryPerformanceCounter(&now_qpc);
    QueryPerformanceFrequency(&freq);
    float dt = last_qpc.QuadPart != 0 ? static_cast<float>(now_qpc.QuadPart - last_qpc.QuadPart) / static_cast<float>(freq.QuadPart) : 0.f;
    last_qpc = now_qpc;
    dt = std::clamp(dt, 0.f, 0.05f);
    const uint64_t now_ms = GetTickCount64();
    g_movement_layer_ms.store(now_ms, std::memory_order_relaxed);
    eldenring::movement::PlayerLocomotion::Instance().stepJump(dt, now_ms, g_ctx);
}

// ---- creative flight (F5 creative mode, double-tap Space) ----------------------------------------------------------------------
std::atomic<bool> g_flying{false};
std::atomic<float> g_fly_h_speed{flight::kHorizontalSpeed};
std::atomic<float> g_fly_v_speed{flight::kVerticalSpeed};
std::atomic<float> g_fly_yaw{0.f};
std::atomic<bool> g_fly_sync{true};

void FlightStep() {
    static LARGE_INTEGER last_qpc{};
    LARGE_INTEGER now_qpc, freq;
    QueryPerformanceCounter(&now_qpc);
    QueryPerformanceFrequency(&freq);
    const float dt = last_qpc.QuadPart != 0 ? static_cast<float>(now_qpc.QuadPart - last_qpc.QuadPart) / static_cast<float>(freq.QuadPart) : 0.f;
    last_qpc = now_qpc;
    const uint64_t now_ms = GetTickCount64();
    g_movement_layer_ms.store(now_ms, std::memory_order_relaxed);
    auto& loco = eldenring::movement::PlayerLocomotion::Instance();
    loco.stepFlight(dt, now_ms, g_ctx);
    g_flying.store(loco.isFlying(), std::memory_order_relaxed);
    g_fly_yaw.store(loco.flyYaw(), std::memory_order_relaxed);
}

// Opens / closes the inventory and drives its virtual pointer. Runs on the key thread before the normal MC input, and takes the
// click edges while the screen is open so nothing else (attack, eating) reacts to them.
void InventoryTick(bool fg) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    eldenring::ui::InventoryController::Instance().tick(fg, WantSuppress(), PlayerDead(), g_melee.inventory(), g_ctx);
}

// Holds the game's own flag bytes while the MC mode is on and gives them back when it is off (eldenring_flags.hpp, REVERSE 40/41): no stamina cost,
// no native HUD, and in creative the "cannot die" bit. Re-forced every tick because the game rewrites the byte itself. mc_er_noflags.txt switches it off.
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

DWORD WINAPI KeyThread(LPVOID) {
    Log("key: KeyThread started (tid %lu)", GetCurrentThreadId());
    eldenring::input::HotkeyDispatcher::Instance().runKeyLoop();
    return 0;
}

bool EnsureMinHook() {
    return eldenring::hooks::GameHooks::Instance().initMinHook();
}

bool FileExists(const std::string& path) {
    const DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadFileAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const size_t got = out.empty() ? 0 : fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

void SetupCreativeProbes() {
    g_cr_log.store(FileExists(g_game_dir + "mc_er_creativelog.txt"));
    uintptr_t kw = 0, fh = 0, fe = 0;
    LocateByPrefix(g_img, sigs::kKillWrapper, kw);
    LocateByPrefix(g_img, sigs::kFallHeight, fh);
    LocateByPrefix(g_img, sigs::kFallTimeExceeded, fe);
    eldenring::hooks::GameHooks::Instance().setupCreativeHooks(
        g_img.base, kw, fh, fe);
    Log("creative: hooks configured (kill_wrapper=%p fall_height=%p fall_time_exceeded=%p)",
        reinterpret_cast<void*>(kw), reinterpret_cast<void*>(fh), reinterpret_cast<void*>(fe));
}

// ---- spawn_bullet request logger (read-only, switch file mc_er_bulletlog.txt) -----------------------------------------------------------
// The game builds a request for every arrow/bolt/spell bullet it fires, the player's own included. Fire one bow/crossbow shot with the switch
// file present and the first requests are written to mc_er.log, so the real BulletParam id and field layout are read, not guessed.
eldenring::bullet::LogBudget g_bullet_budget(40);
std::atomic<unsigned> g_bullet_seen{0};

void LogBulletRequest(void* manager, const void* request, const void* r9, void* ret_addr, uint32_t handle) {
    const unsigned seq = g_bullet_seen.fetch_add(1) + 1;
    if (!g_bullet_budget.take()) return;
    uint8_t body[eldenring::bullet::kRequestBytes] = {};
    if (!SafeCopy(reinterpret_cast<uintptr_t>(request), body, sizeof(body))) {
        Log("bullet #%u: the request at %p cannot be read", seq, request);
        return;
    }
    const eldenring::bullet::Fields f = eldenring::bullet::decode(body, sizeof(body));
    Log("bullet #%u: manager=%p request=%p r9=%p caller RVA 0x%llX tid=%lu -> handle 0x%08X", seq, manager, request, r9,
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ret_addr) - g_img.base), static_cast<unsigned long>(GetCurrentThreadId()), handle);
    Log("bullet #%u: owner=0x%016llX param row (+0x08)=0x%016llX id@+1C=%u flags@+44=0x%X pos=(%.2f %.2f %.2f) fwd=(%.3f %.3f %.3f)", seq,
        static_cast<unsigned long long>(f.owner), static_cast<unsigned long long>(f.row_id), f.id_at_1c, f.flags_at_44, f.position[0], f.position[1],
        f.position[2], f.forward[0], f.forward[1], f.forward[2]);
    for (const std::string& line : eldenring::bullet::hexDump(body, sizeof(body))) Log("bullet #%u:   %s", seq, line.c_str());
    uint8_t extra[0x40] = {};
    if (r9 != nullptr && SafeCopy(reinterpret_cast<uintptr_t>(r9), extra, sizeof(extra))) {
        for (const std::string& line : eldenring::bullet::hexDump(extra, sizeof(extra))) Log("bullet #%u: r9 %s", seq, line.c_str());
    }
    // A real request carries a non-zero pointer at +0xB0 (to an object on the caller's stack) that our replay sets to 0, and the replay flies along the shooter's
    // body instead of the matrix: does that object hold the aim? Dumped with its floats so a vector near the camera's forward can be spotted (REVERSE 35.11).
    {
        auto dump = [&](const char* label, uintptr_t at, size_t bytes) {
            std::vector<uint8_t> buf(bytes, 0);
            if (at == 0 || !SafeCopy(at, buf.data(), bytes)) {
                Log("bullet #%u: %s at %p cannot be read", seq, label, reinterpret_cast<void*>(at));
                return;
            }
            Log("bullet #%u: %s at %p (%zu bytes):", seq, label, reinterpret_cast<void*>(at), bytes);
            for (size_t row = 0; row < bytes; row += 16) {
                float v[4];
                std::memcpy(v, buf.data() + row, sizeof(v));
                char line[200];
                int n = snprintf(line, sizeof(line), "+0x%03zX:", row);
                for (size_t i = 0; i < 16; ++i) n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " %02X", buf[row + i]);
                snprintf(line + n, sizeof(line) - static_cast<size_t>(n), "  | %10.4f %10.4f %10.4f %10.4f", v[0], v[1], v[2], v[3]);
                Log("bullet #%u:   %s", seq, line);
            }
        };
        uint64_t sub = 0;
        std::memcpy(&sub, body + 0xB0, sizeof(sub));
        dump("sub-object at request+0xB0", static_cast<uintptr_t>(sub), 0x100);
        dump("r9 context", reinterpret_cast<uintptr_t>(r9), 0x100);
        uint64_t ctx_ptr = 0;
        if (r9 != nullptr && SafeCopy(reinterpret_cast<uintptr_t>(r9) + 0x28, &ctx_ptr, sizeof(ctx_ptr))) dump("object at r9+0x28", static_cast<uintptr_t>(ctx_ptr), 0x80);
    }
}

std::atomic<uint64_t> g_last_bullet_fire_ms{0};

// Bullet template management delegated to CombatService
void SaveBulletTemplate(const uint8_t* body) {
    eldenring::combat::CombatService::Instance().saveBulletTemplate(body, g_game_dir);
}

void LoadBulletTemplate() {
    eldenring::combat::CombatService::Instance().loadBulletTemplate(g_game_dir);
}

void RecordBulletTemplate(const void* request) {
    eldenring::combat::CombatService::Instance().recordBulletTemplate(request);
}

bool SpawnBulletSafe(void* manager, uint32_t* out_handle, void* request, void* ctx) {
    return eldenring::hooks::GameHooks::Instance().callSpawnBullet(manager, out_handle, request, ctx) != nullptr;
}

void AimCalStart(uint64_t manager, uint32_t handle, const float dir[3]) {
    eldenring::combat::CombatService::Instance().aimCalStart(manager, handle, dir);
}

void AimCalTick() {
    eldenring::combat::CombatService::Instance().aimCalTick();
}

void RunBulletFire() {
    eldenring::combat::CombatService::Instance().runBulletFire(g_ctx);
}

bool QueueBulletFire(float mc_damage) {
    return eldenring::combat::CombatService::Instance().queueBulletFire(mc_damage, g_ctx);
}

// CS hitscan: spread the shot, pick the hostile under the crosshair, check walls and enqueue the damage.
// Runs on the input thread; the queue is drained on the game thread by DrainOnce.
bool FireCsShot(float damage, float spread, float range, uint32_t tracer_frequency, uint8_t weapon_slot) {
#if ENABLE_CS2_VIEWMODEL
    eldenring::cs::CsShotParams params;
    params.damage = damage;
    params.spread_deg = spread;
    params.max_range_m = range;
    // Tracers are disabled by design; the weapon registry also keeps tracer_frequency at 0.
    (void)tracer_frequency;
    params.tracer_frequency = 0;
    params.weapon_slot = weapon_slot;
    return eldenring::combat::CombatService::Instance()
        .fireCsShot(params, g_queue, NowTick(), eldenring::RuntimeContext::Instance())
        .fired;
#else
    (void)damage;
    (void)spread;
    (void)range;
    (void)tracer_frequency;
    (void)weapon_slot;
    return false;
#endif
}

void SetupBulletLog() {
    const bool proj_log = FileExists(g_game_dir + "mc_er_bulletlog.txt");
    const bool arrow_dmg = FileExists(g_game_dir + "mc_er_arrowdmg.txt");
    const bool fire_enabled = FileExists(g_game_dir + "mc_er_bulletfire.txt");
    const bool cs_shot_log = FileExists(g_game_dir + "mc_er_csshotlog.txt");
    g_proj_log.store(proj_log);
    g_arrow_damage.store(arrow_dmg);
    g_bullet_fire_enabled.store(fire_enabled);
    eldenring::combat::CombatService::Instance().setAimCalEnabled(FileExists(g_game_dir + "mc_er_aimcal.txt"));
    eldenring::combat::CombatService::Instance().setArrowDamage(arrow_dmg);
    eldenring::combat::CombatService::Instance().setBulletFireEnabled(fire_enabled);
    eldenring::combat::CombatService::Instance().setCsShotLogEnabled(cs_shot_log);
    if (cs_shot_log) Log("cs: mc_er_csshotlog.txt: every CS hitscan shot is logged");
    if (arrow_dmg) Log("bullet: mc_er_arrowdmg.txt: the player's bolts do MC bolt damage (9, balanced like the diamond sword)");
    if (fire_enabled) LoadBulletTemplate();
    if (!fire_enabled && !proj_log) return;
    uintptr_t target = 0;
    if (!EnsureMinHook()) {
        Log("bullet: MH_Initialize failed, the request logger is not installed");
    } else if (!LocateByPrefix(g_img, sigs::kSpawnBullet, target)) {
        Log("bullet: spawn_bullet signature not unique, the request logger is not installed");
    } else if (!eldenring::hooks::GameHooks::Instance().setupBulletLog(g_img.base, target)) {
        Log("bullet: hooking spawn_bullet at %p failed", reinterpret_cast<void*>(target));
    } else {
        Log("bullet: spawn_bullet request logger at %p (RVA 0x%llX): the first 40 requests are written here%s", reinterpret_cast<void*>(target),
            static_cast<unsigned long long>(target - g_img.base),
            fire_enabled ? "; mc_er_bulletfire.txt: F3 shoots one bolt of ours from the saved template (a real crossbow shot creates or updates it)" : " (read-only)");
    }
}

void SetupGameHooksCallbacks() {
    eldenring::hooks::GameHooksCallbacks cb;
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    cb.get_game_tid = []() -> uint32_t { return g_game_tid.load(std::memory_order_relaxed); };
    cb.get_player_chr = PlayerChrPtr;
    cb.safe_copy = [](uintptr_t a, void* d, size_t s) { return SafeCopy(a, d, s); };

    cb.on_camera_step_pre = [](float dt) {
        if (g_bullet_fire_pending.load(std::memory_order_relaxed) &&
            GetCurrentThreadId() == g_game_tid.load(std::memory_order_relaxed) &&
            g_bullet_fire_pending.exchange(false)) {
            RunBulletFire();
        }
        FlightStep();
        BlocksCollisionStep();
        McJumpStep();
        GroundSprintStep(dt);
        ShadowGroundStep();
        RestoreCamKeep();
        {
            const uintptr_t p = PlayerChrPtr();
            float p_feet[3] = {};
            if (p != 0 && detail::readPhysicsPosition(g_reader, g_img.base, p, p_feet)) {
                EnemiesBlockCollisionStep(p_feet);
            }
        }
        VirtualSummonsStep(dt);
    };

    cb.check_chr_cam = [](uint64_t self, uintptr_t& addr, float before[3]) -> bool {
        return ChrCamPosAddress(addr) && addr == static_cast<uintptr_t>(self) + layout::kCamMatrix + 0x30 &&
               SafeCopy(addr, before, sizeof(float) * 3);
    };

    cb.on_camera_step_post = [](bool have, uintptr_t addr, const float before[3]) {
        AfterCameraStep(have, addr, before);
    };

    cb.on_clamp_tick = [](uintptr_t module) {
        if (g_damage_enabled.load(std::memory_order_relaxed) && g_queue.pending() > 0) {
            DrainOnce(module);
        }
        if (g_summon_pending.load(std::memory_order_relaxed) && g_summon_pending.exchange(false)) {
            RunSummonExperiment();
        }
        if (g_bullet_fire_pending.load(std::memory_order_relaxed) && g_bullet_fire_pending.exchange(false)) {
            RunBulletFire();
        }
        AimCalTick();
        if (g_heal_pending.load(std::memory_order_relaxed) > 0) {
            uint64_t owner = 0;
            if (SafeCopy(module + layout::kOwnerInDataModule, &owner, sizeof(owner)) && owner != 0 && owner == PlayerChrPtr()) {
                const int h = g_heal_pending.exchange(0);
                if (h > 0) GiveHp(module, h);
            }
        }
    };

    cb.player_hit_survival = [](void* mod, void* atk, uint8_t* ctx, uint8_t a5, uint32_t& heal_to) -> bool {
        if (eldenring::combat::CombatService::Instance().isForcedDamageArmed()) return false;
        return PlayerHitSurvival(mod, atk, ctx, a5, heal_to);
    };

    cb.log_player_hit = [](void* mod, void* atk, uint8_t* ctx, uint8_t a5) {
        if (!eldenring::combat::CombatService::Instance().isForcedDamageArmed() && g_log_player_hits.load(std::memory_order_relaxed)) {
            LogPlayerHit(mod, atk, ctx, a5);
        }
    };

    cb.handle_projectile_hit = [](void* mod, void* atk, uint8_t* ctx) {
        if (!eldenring::combat::CombatService::Instance().isForcedDamageArmed() && (g_proj_log.load(std::memory_order_relaxed) ||
                                g_arrow_damage.load(std::memory_order_relaxed) ||
                                g_bullet_fire_enabled.load(std::memory_order_relaxed))) {
            HandleProjectileHit(mod, atk, ctx);
        }
    };

    cb.handle_forced_damage = [](uint8_t* ctx) {
        eldenring::combat::CombatService::Instance().handleForcedDamage(ctx);
    };

    cb.override_final_damage = [](uint8_t* ctx, uintptr_t atk, uintptr_t vic, int val, int* eng_val) -> bool {
        return overrideFinalDamage(ctx, atk, vic, val, eng_val);
    };

    cb.on_totem_popped_heal = [](uint32_t heal_to) {
        const uintptr_t data = DataModuleOfChr(PlayerChrPtr());
        int32_t hp = 0;
        if (data != 0 && SafeCopy(data + layout::kDataHp, &hp, sizeof(hp)) && static_cast<int32_t>(heal_to) > hp) {
            GiveHp(data, static_cast<int>(heal_to) - hp);
        }
    };

    cb.should_skip_kill_chr = [](void* chr) -> bool {
        if (reinterpret_cast<uintptr_t>(chr) == PlayerChrPtr() && PlayerChrPtr() != 0) {
            const bool recent = GetTickCount64() - g_movement_layer_ms.load(std::memory_order_relaxed) < 3000;
            const bool protect = creative::shouldSkipPlayerKill(g_fall_protect.load(std::memory_order_relaxed),
                                                                g_mc_mode.load(std::memory_order_relaxed),
                                                                BlocksPresent() || recent, PlayerDead() ? 0 : 1);
            const unsigned n = ++g_kills_skipped;
            if (n <= 20) Log("kill: the game kills the player: %s", protect ? "SKIPPED (fall protection)" : "allowed");
            return protect;
        }
        return false;
    };

    cb.should_skip_hit_vfx = [](void* a1, void* a2, uint8_t* ctx, void*) -> bool {
        uint64_t victim = 0, player = 0;
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        const bool known = world != 0 && SafeCopy(reinterpret_cast<uintptr_t>(ctx) + layout::kHitVictim, &victim, sizeof(victim)) &&
                           SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0;
        static std::atomic<unsigned> logged{0};
        if (known && logged.load() < 12 && (victim == player || logged.load() < 4)) {
            ++logged;
            Log("hit vfx: call a1=%p a2=%p ctx victim=%p player=%p -> %s", a1, a2, reinterpret_cast<void*>(victim), reinterpret_cast<void*>(player),
                victim == player ? "PLAYER IS THE VICTIM" : "other victim");
        }
        if (known && victim == player && (g_no_player_hit_vfx.load(std::memory_order_relaxed) || g_first_person.load(std::memory_order_relaxed))) {
            ++g_hitvfx_skipped;
            return true;
        }
        return false;
    };

    cb.should_skip_hit_react = [](void* self, void* out) -> bool {
        if (g_no_stagger.load(std::memory_order_relaxed) && g_mc_mode.load(std::memory_order_relaxed) && IsPlayerActionModule(self)) {
            if (out != nullptr) *static_cast<uint8_t*>(out) = 0;
            if (++g_stagger_skipped <= 10) Log("no_stagger: skipped hit reaction picker for the player");
            return true;
        }
        return false;
    };

    cb.on_render_cam_copy = [](void*, const std::function<void()>& orig) {
        HideSummonModels();
        if (g_native_hide_wanted.load(std::memory_order_relaxed)) {
            const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
            uint64_t player = 0;
            if (world != 0 && SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0) {
                UpdateNativeModel(static_cast<uintptr_t>(player), true);
            }
        }
        uintptr_t pos_addr = 0;
        uintptr_t axes_addr = 0;
        float saved[3] = {};
        float saved_axes[8] = {};
        if (g_first_person.load(std::memory_order_relaxed)) {
            const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
            uint64_t cam = 0, player = 0;
            float feet[3];
            if (world != 0 && SafeCopy(world + layout::kChrCamInWorldChrMan, &cam, sizeof(cam)) && cam != 0 &&
                SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) && player != 0 &&
                detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet) &&
                SafeCopy(static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30, saved, sizeof(saved))) {
                float eye[3];
                firstPersonEye(feet, g_eye_height.load(), eye);
                pos_addr = static_cast<uintptr_t>(cam) + layout::kCamMatrix + 0x30;
                SafeWrite(pos_addr, eye, sizeof(eye));

                float hurt = 0.f, side = 1.f;
                ReadHurt(hurt, side);
                if (pos_addr != 0 && hurt > 0.f) {
                    const uintptr_t axes = static_cast<uintptr_t>(cam) + layout::kCamMatrix;
                    if (SafeCopy(axes, saved_axes, sizeof(saved_axes))) {
                        const float a = eldenring::fx::hurtTiltRadians(hurt, side), c = std::cos(a), s = std::sin(a);
                        float rolled[8];
                        for (int i = 0; i < 3; ++i) {
                            rolled[i] = saved_axes[i] * c + saved_axes[4 + i] * s;
                            rolled[4 + i] = saved_axes[4 + i] * c - saved_axes[i] * s;
                        }
                        rolled[3] = saved_axes[3];
                        rolled[7] = saved_axes[7];
                        if (SafeWrite(axes, rolled, sizeof(rolled))) {
                            axes_addr = axes;
                        }
                    }
                }
            }
        }
        orig();
        if (pos_addr != 0) SafeWrite(pos_addr, saved, sizeof(saved));
        if (axes_addr != 0) SafeWrite(axes_addr, saved_axes, sizeof(saved_axes));
    };

    cb.on_spawn_bullet = [](const void* request) {
        eldenring::combat::CombatService::Instance().onSpawnBullet(request);
    };

    cb.on_spawn_bullet_post = [](void* manager, void* request, void* r9, uint32_t* out_handle) {
        uint32_t handle = 0xFFFFFFFFu;
        if (out_handle != nullptr) SafeCopy(reinterpret_cast<uintptr_t>(out_handle), &handle, sizeof(handle));
        LogBulletRequest(manager, request, r9, nullptr, handle);
        if (!eldenring::bullet::spawnFailed(handle) && !eldenring::combat::CombatService::Instance().aimCalState().active) {
            uint8_t req[eldenring::bullet::kRequestBytes] = {};
            if (SafeCopy(reinterpret_cast<uintptr_t>(request), req, sizeof(req))) {
                const eldenring::bullet::Fields f = eldenring::bullet::decode(req, sizeof(req));
                if (f.valid) AimCalStart(reinterpret_cast<uint64_t>(manager), handle, f.forward);
            }
        }
    };

    cb.is_fall_damage_disabled = []() -> bool {
        return creative::zeroesFall(CurrentGameMode()) && g_mc_mode.load(std::memory_order_relaxed);
    };

    cb.should_block_creative_kill = [](void* chr) -> bool {
        const uintptr_t player = PlayerChrPtr();
        if (creative::isPlayer(reinterpret_cast<uintptr_t>(chr), player)) {
            const bool block = creative::shouldBlockPlayerKill(CurrentGameMode(), g_mc_mode.load(std::memory_order_relaxed), PlayerDead() ? 0 : 1);
            static std::atomic<unsigned> logged{0};
            if (block || g_cr_log.load(std::memory_order_relaxed)) {
                if (++logged <= 30) {
                    Log("%s%s", creative::formatKill(0, true).c_str(),
                        block ? ": SKIPPED (creative mode, the player still has hit points)" : "");
                }
            }
            return block;
        }
        return false;
    };

    cb.should_deny_fall_time_exceeded = [](void* fall_module, float threshold) -> bool {
        const bool held_up = g_fall_protect.load(std::memory_order_relaxed) &&
                             (BlocksPresent() || GetTickCount64() - g_movement_layer_ms.load(std::memory_order_relaxed) < 3000);
        if (!creative::shouldDenyLongFall(CurrentGameMode(), g_mc_mode.load(std::memory_order_relaxed), creative::isPlayer(ModuleOwner(fall_module), PlayerChrPtr()), held_up)) return false;
        static std::atomic<unsigned> logged{0};
        if (++logged <= 10) Log("creative: the fall lasted %.2f s (threshold %.2f s): answered no for the player", FallTimer(PlayerChrPtr()), static_cast<double>(threshold));
        return true;
    };

    eldenring::hooks::GameHooks::Instance().setCallbacks(cb);
}

void SetupDamage() {
    if (!FileExists(g_game_dir + "mc_er_damage.txt")) {
        Log("damage: disabled (no mc_er_damage.txt)");
        return;
    }
    std::vector<uint8_t> bytes;
    if (!ReadFileAll(g_game_dir + "mc_er_hit.bin", bytes)) {
        Log("damage: mc_er_hit.bin missing, damage disabled");
        return;
    }
    g_template = HitTemplate::fromBytes(bytes.data(), bytes.size());
    if (!g_template) {
        Log("damage: mc_er_hit.bin has %zu bytes, expected %zu; damage disabled", bytes.size(), layout::kHitTemplateSize);
        return;
    }
    uintptr_t clamp = 0;
    if (!LocateByPrefix(g_img, sigs::kClampHp, clamp)) {
        Log("damage: ClampHP signature not unique, damage disabled");
        return;
    }
    if (!EnsureMinHook()) {
        Log("damage: MH_Initialize failed");
        return;
    }

    SetupGameHooksCallbacks();

    uintptr_t pdc = 0;
    if (!LocateByPrefix(g_img, sigs::kProcessDamageContext, pdc)) {
        Log("damage: ProcessDamageContext signature not unique, final damage stays engine-computed");
    } else {
        g_pdc_hooked.store(true);
        Log("damage: ProcessDamageContext hooked at %p (RVA 0x%llX): final damage is replaced for our own hits", reinterpret_cast<void*>(pdc),
            static_cast<unsigned long long>(pdc - g_img.base));
    }
    uintptr_t brk = 0;
    if (FileExists(g_game_dir + "mc_er_noknockdownfix.txt")) {
        g_knockdown_fix.store(false);
        Log("knockdown: mc_er_noknockdownfix.txt present, the player keeps the game's heavy reactions");
    } else {
        if (!LocateByPrefix(g_img, sigs::kBreakTest, brk)) {
            Log("knockdown: break test signature not unique, the player keeps the game's heavy reactions");
        } else {
            Log("knockdown: break test hooked at %p (RVA 0x%llX): in MC mode a hit on the player only flinches", reinterpret_cast<void*>(brk),
                static_cast<unsigned long long>(brk - g_img.base));
        }
    }
    uintptr_t guard = 0, vf7 = 0;
    if (FileExists(g_game_dir + "mc_er_noguardhook.txt")) {
        Log("shield: mc_er_noguardhook.txt present, the engine block path and the knockback fix are off (the damage hook still zeroes blocked hits)");
    } else {
        if (!LocateByPrefix(g_img, sigs::kGuardCheck, guard)) {
            Log("shield: GuardCheck signature not unique, blocked hits are only zeroed in the damage hook");
        } else {
            Log("shield: GuardCheck hooked at %p (RVA 0x%llX): a blocked hit takes the engine's own guard path", reinterpret_cast<void*>(guard),
                static_cast<unsigned long long>(guard - g_img.base));
        }
        if (!LocateByPrefix(g_img, sigs::kDamageVfunc7, vf7)) {
            Log("shield: damage vfunc[7] signature not unique, a blocked hit still pushes the player");
        } else {
            Log("shield: damage vfunc[7] hooked at %p (RVA 0x%llX): a blocked hit does not push the player", reinterpret_cast<void*>(vf7),
                static_cast<unsigned long long>(vf7 - g_img.base));
        }
    }
    uintptr_t rcc = 0;
    if (!LocateByPrefix(g_img, sigs::kRenderCameraCopy, rcc)) {
        Log("first person: render camera copy signature not unique, experiment unavailable");
    } else {
        Log("first person: render camera copy hooked at %p (RVA 0x%llX); first_person=1 or F10 turns the experiment on", reinterpret_cast<void*>(rcc),
            static_cast<unsigned long long>(rcc - g_img.base));
        g_summon_hide.store(!FileExists(g_game_dir + "mc_er_nosummonhide.txt"));
        if (g_summon_hide.load()) Log("summon hide: on: team 47 models, shadows, and spirit particles are cleared before each frame");
    }
    uintptr_t hp_fn = 0;
    if (LocateByPrefix(g_img, sigs::kApplyHpChange, hp_fn)) {
        g_apply_hp = reinterpret_cast<ApplyHpFn>(hp_fn);
        Log("survival: ApplyHPChange at %p (RVA 0x%llX): food, regeneration and the totem heal through it", reinterpret_cast<void*>(hp_fn),
            static_cast<unsigned long long>(hp_fn - g_img.base));
    } else {
        Log("survival: ApplyHPChange signature not unique, no healing (the totem still clamps lethal damage)");
    }
    g_log_player_hits.store(FileExists(g_game_dir + "mc_er_hitlog.txt"));
    if (g_log_player_hits.load()) Log("hitlog: logging the HitContext of every hit the player takes");
    uintptr_t vfx = 0;
    if (FileExists(g_game_dir + "mc_er_hitvfx.txt")) {
        if (!LocateByPrefix(g_img, sigs::kHitVfxSpawn, vfx)) {
            Log("hit vfx: spawner signature not unique, the player's blood effect cannot be removed");
        } else {
            Log("hit vfx: spawner hooked at %p (RVA 0x%llX); no_player_hit_vfx=1 or first person removes the player's hit effect",
                reinterpret_cast<void*>(vfx), static_cast<unsigned long long>(vfx - g_img.base));
        }
    }
    uintptr_t step = 0;
    if (FileExists(g_game_dir + "mc_er_camwatch.txt")) {
        Log("camwatch: diagnostic on (mc_er_camwatch.txt): the camera update hook is not installed, writers of ChrCam position are logged");
        CreateThread(nullptr, 0, CamWatchThread, nullptr, 0, nullptr);
    } else if (!LocateByPrefix(g_img, sigs::kCameraStepExecute, step)) {
        Log("camstep: camera update signature not unique, hit effects stay at the third-person position in first person");
    } else {
        g_camstep_installed.store(true);
        Log("camstep: camera update hooked at %p (RVA 0x%llX): first person keeps the eye position for the whole frame (fp_persist=0 turns it off)",
            reinterpret_cast<void*>(step), static_cast<unsigned long long>(step - g_img.base));
    }
    uintptr_t gate = 0, freeze = 0;
    if (!LocateByPrefix(g_img, sigs::kIsInputBlocked, gate)) {
        Log("inventory: input gate signature not unique, the character keeps moving while the inventory is open");
    } else {
        Log("inventory: input gate hooked at %p (RVA 0x%llX)", reinterpret_cast<void*>(gate), static_cast<unsigned long long>(gate - g_img.base));
    }
    if (!LocateByPrefix(g_img, sigs::kMenuFreezesCamera, freeze)) {
        Log("inventory: camera freeze signature not unique, the camera keeps turning while the inventory is open");
    } else {
        Log("inventory: camera freeze hooked at %p (RVA 0x%llX)", reinterpret_cast<void*>(freeze), static_cast<unsigned long long>(freeze - g_img.base));
    }
    uintptr_t react = 0, heavy = 0;
    if (!LocateByPrefix(g_img, sigs::kHitReactDefault, react) || !LocateByPrefix(g_img, sigs::kHitReactHeavy, heavy)) {
        Log("no_stagger: hit reaction signatures not unique, the experiment is unavailable");
    } else {
        Log("no_stagger: reaction pickers hooked at %p / %p (RVA 0x%llX / 0x%llX); no_stagger=1 in mc_er_steve.txt turns the experiment on",
            reinterpret_cast<void*>(react), reinterpret_cast<void*>(heavy), static_cast<unsigned long long>(react - g_img.base),
            static_cast<unsigned long long>(heavy - g_img.base));
    }
    uintptr_t kill = 0;
    if (!LocateByPrefix(g_img, sigs::kKillChr, kill)) {
        Log("kill: signature not unique, fall protection is unavailable");
    } else {
        Log("kill: the kill-character function is hooked at %p (RVA 0x%llX); while blocks exist or our jump is recent the player's kill is skipped", reinterpret_cast<void*>(kill),
            static_cast<unsigned long long>(kill - g_img.base));
    }
    if (FileExists(g_game_dir + "mc_er_nolos.txt")) {
        Log("los: disabled by mc_er_nolos.txt");
    } else {
        uintptr_t ray = 0;
        if (!LocateByPrefix(g_img, sigs::kRaycastWrapper, ray)) {
            Log("los: raycast wrapper signature not unique, walls do not block hits");
        } else {
            eldenring::combat::CombatService::Instance().setRaycastFn(reinterpret_cast<RaycastFn>(ray));
            g_los_enabled.store(true);
            Log("los: raycast wrapper at %p (RVA 0x%llX), filter 0x%X, walls block hits", reinterpret_cast<void*>(ray),
                static_cast<unsigned long long>(ray - g_img.base), los::kStaticGeometryFilter);
        }
    }
    g_require_victim_updating.store(FileExists(g_game_dir + "mc_er_require_victim.txt"));
    g_damage_enabled.store(true);
    Log("damage: enabled; ClampHP hooked at %p (RVA 0x%llX); press F8 to hit the nearest hostile enemy (require_victim_updating=%d)", reinterpret_cast<void*>(clamp),
        static_cast<unsigned long long>(clamp - g_img.base), g_require_victim_updating.load() ? 1 : 0);

    eldenring::hooks::GameHooks::Instance().setupDamageHooks(
        g_img.base, clamp, pdc, brk, guard, vf7, rcc, vfx, step, gate, freeze, react, heavy, kill);
}

// ---- overlay -------------------------------------------------------------------------------------------------

// Runs on the Present thread. The HUD is hidden whenever the game shows its own full-screen UI.
// Clears (hide) or restores (show) the "drawn" bit of every part of the player's native model. Called every frame from
// the Present thread while the feature is on; the part list is re-read each time, so parts the game replaced are
// never written through a stale address.
// Puts back every bit we cleared, wherever it was (not only in the slots selected right now).
void RestoreNativeModel() {
    eldenring::ui::RestoreNativeModel(g_hidden_flags, g_native_mutex);
}

void UpdateNativeModel(uintptr_t player, bool hide) {
    eldenring::ui::UpdateNativeModel(player, hide, g_reader, g_img.base, g_ctx, g_hidden_flags, g_native_mutex);
}

bool HudProvider(erov::HudState& out, erov::SteveState& steve) {
    {   // the mobs over the summons (mc_er_summonhide.txt): only while MC mode is on, otherwise the wolves are not drawn at all and nothing replaces them
        std::vector<eldenring::mobs::MobSnapshot> mobs;
        std::vector<eldenring::summon::ArrowDraw> arrows;
        if (g_summon_hide.load(std::memory_order_relaxed) && g_mc_mode.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> g(g_summon_mutex);
            mobs = g_summon_list;
            arrows = g_summon_arrows;
        }
        erov::SetMobs(mobs);
        erov::SetArrows(arrows);
    }
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    if (world == 0) return false;
    uint64_t player = 0;
    Vitals v;
    if (!SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player)) || player == 0 ||
        !readVitals(g_reader, g_img.base, static_cast<uintptr_t>(player), v)) {
        return false;
    }
    MenuState ms;
    LoadingState ls;
    float fade = 0.f;
    const bool have_state = readMenuState(g_reader, readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan), ms) &&
                            readLoadingState(g_reader, readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper), ls) &&
                            readFadeAlpha(g_reader, readSingleton(g_reader, g_img.base, g_rva_fade, sigs::kCSFade), fade);

    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        eldenring::ui::PopulateHudStateFromContext(out, g_ctx);
        static int last_logged_armor_pts = -1;
        if (out.armor_points != last_logged_armor_pts) {
            Log("ARMOR-HUD: total_armor_points=%d (reduction=%.1f%%, inv_open=%d)",
                out.armor_points, std::min(80.f, out.armor_points * 4.0f), g_inv_open.load());
            last_logged_armor_pts = out.armor_points;
        }
    }

    out.hp = v.hp;
    out.max_hp = v.max_hp;
    out.show = have_state && !ms.menu_focused && !ms.popup_open && !ls.screen_loading && fade < 0.02f;

    if (g_hide_native.load()) {
#if ENABLE_CS2_VIEWMODEL
        const bool want_hidden = (out.mc_mode || out.cs_mode) && have_state && !ls.screen_loading;
#else
        const bool want_hidden = out.mc_mode && have_state && !ls.screen_loading;
#endif
        g_native_hide_wanted.store(want_hidden);
        UpdateNativeModel(static_cast<uintptr_t>(player), want_hidden);
        static uint64_t last_report_ms = 0;
        const uint64_t now_ms = GetTickCount64();
        if (now_ms - last_report_ms >= 1000) {
            last_report_ms = now_ms;
            const unsigned n = g_native_reshown.exchange(0);
            if (n != 0) Log("native: the game turned hidden parts back on %u time(s) in the last second", n);
            const unsigned sk = g_hitvfx_skipped.exchange(0);
            if (sk != 0) Log("hit vfx: skipped the player's hit effect %u time(s)", sk);
        }
    }

    steve.draw = false;
    steve.cam_valid = false;
    CameraPose cam;
    float feet[3], q[4];
    if (g_steve_enabled.load() && readCamera(g_reader, g_img.base, world, cam) &&
        detail::readPhysicsPosition(g_reader, g_img.base, static_cast<uintptr_t>(player), feet) &&
        detail::readPhysicsOrientation(g_reader, g_img.base, static_cast<uintptr_t>(player), q)) {
        steve.draw = true;
        steve.cam_valid = true;
        steve.cam.right = {cam.right[0], cam.right[1], cam.right[2]};
        steve.cam.up = {cam.up[0], cam.up[1], cam.up[2]};
        steve.cam.forward = {cam.forward[0], cam.forward[1], cam.forward[2]};
        if (g_first_person.load(std::memory_order_relaxed)) {
            steve.cam.position = {feet[0], feet[1] + g_eye_height.load(std::memory_order_relaxed), feet[2]};
        } else {
            steve.cam.position = {cam.position[0], cam.position[1], cam.position[2]};
        }
        steve.fov_y = cam.fov_y * g_sprint_fov.load(std::memory_order_relaxed);
        steve.feet[0] = feet[0];
        steve.feet[1] = feet[1];
        steve.feet[2] = feet[2];
        steve.yaw = eldenring::render::yawFromQuat(q[0], q[1], q[2], q[3]) + g_steve_yaw_offset;
        if (g_flying.load(std::memory_order_relaxed)) steve.yaw = g_fly_yaw.load(std::memory_order_relaxed); // the game does not turn a character that never sees a key
        float grace_dist = 999.f;
        const bool near_grace = findNearestGrace(g_reader, g_img.base, world, feet, 2.8f, &grace_dist);
        steve.sleeping = near_grace && (ms.menu_focused || fade >= 0.02f);
    }
    const bool steve_dead_now = v.max_hp > 0 && v.hp <= 0;
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        eldenring::ui::PopulateSteveStateFromContext(steve, g_ctx, steve_dead_now, steve.sleeping);
    }
    {
        static bool was_dead = false;
        if (steve.dead && !was_dead) eraudio::Play("entity.player.death");
        was_dead = steve.dead;
    }
    if (steve.sleeping) {
        steve.first_person = false;
        steve.draw = true; // Always draw Steve when sleeping at a grace
    } else if (g_slot_probe.load() >= 0 || g_first_person.load()) {
        steve.draw = false; // slot probing: show only the native model, with the one slot missing
    }
    {   // why Steve is or is not drawn, logged when any of the inputs changes
        struct Seen { bool draw, enabled, first_person, show, mc, cam_ok; int slot_probe; bool valid; };
        static Seen last{};
        const Seen now{steve.draw, g_steve_enabled.load(), g_first_person.load(), out.show, out.mc_mode, steve.cam_valid, g_slot_probe.load(), true};
        if (!last.valid || now.draw != last.draw || now.enabled != last.enabled || now.first_person != last.first_person || now.show != last.show ||
            now.mc != last.mc || now.cam_ok != last.cam_ok || now.slot_probe != last.slot_probe) {
            Log("STEVE-STATE: draw=%d enabled=%d cam_valid=%d first_person=%d slot_probe=%d show=%d mc_mode=%d dead=%d", now.draw ? 1 : 0, now.enabled ? 1 : 0,
                now.cam_ok ? 1 : 0, now.first_person ? 1 : 0, now.slot_probe, now.show ? 1 : 0, now.mc ? 1 : 0, steve.dead ? 1 : 0);
            last = now;
        }
    }
    return true;
}

void SetupOverlay() {
    if (!FileExists(g_game_dir + "mc_er_overlay.txt")) {
        Log("overlay: disabled (no mc_er_overlay.txt)");
        return;
    }
    if (!EnsureMinHook()) {
        Log("overlay: MH_Initialize failed");
        return;
    }
    if (FileExists(g_game_dir + "mc_er_steve.txt")) {
        erov::SteveConfig cfg;
        std::vector<uint8_t> bytes;
        if (ReadFileAll(g_game_dir + "mc_er_steve.txt", bytes)) {
            const std::string text(bytes.begin(), bytes.end());
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                const std::string line = text.substr(pos, end - pos);
                pos = end + 1;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq);
                const float value = static_cast<float>(atof(line.c_str() + eq + 1));
                if (key == "occlusion") cfg.occlusion = value != 0.f;
                else if (key == "debug") cfg.debug = static_cast<int>(value);
                else if (key == "depthview_gain") cfg.depthview_gain = value;
                else if (key == "depth_const") cfg.depth_const = value;
                else if (key == "rel_bias") cfg.rel_bias = value;
                else if (key == "abs_bias") cfg.abs_bias = value;
                else if (key == "scene_height") cfg.scene_height = value;
                else if (key == "yaw_offset_deg") g_steve_yaw_offset = value * 3.14159265f / 180.f;
                else if (key == "mob_yaw_offset_deg") g_mob_yaw_offset = value * 3.14159265f / 180.f;
                else if (key == "hide_native") g_hide_native.store(value != 0.f);
                else if (key == "fp_persist") g_fp_persist.store(value != 0.f);
                else if (key == "sound_volume") g_sound_volume.store(value);
                else if (key == "no_player_hit_vfx") g_no_player_hit_vfx.store(value != 0.f);
                else if (key == "first_person") g_first_person.store(value != 0.f);
                else if (key == "shadow") g_shadow_enabled.store(value != 0.f);
                else if (key == "bolt_attach_poly") eldenring::combat::CombatService::Instance().setBoltAttachPoly(static_cast<int>(value));
                else if (key == "eye_height") g_eye_height.store(value);
                else if (key == "fall_reset") g_fall_reset.store(value != 0.f);
                else if (key == "fall_hold") g_fall_hold.store(std::clamp(value, 0.f, 2.8f));
                else if (key == "fall_protect") g_fall_protect.store(value != 0.f);
                else if (key == "fly_sync") g_fly_sync.store(value != 0.f);
                else if (key == "fly_speed") g_fly_h_speed.store(std::clamp(value, 1.f, 40.f));
                else if (key == "fly_vspeed") g_fly_v_speed.store(std::clamp(value, 1.f, 40.f));
                else if (key == "mc_jump") g_mc_jump.store(value != 0.f);
                else if (key == "mc_jump_key") g_mc_jump_vk.store(static_cast<int>(value));
                else if (key == "mc_mode") {
#if ENABLE_CS2_VIEWMODEL
                    g_mc_mode.store(false); // CS2 build: MC mode is not available
#else
                    g_mc_mode.store(value != 0.f);
#endif
                }
                else if (key == "mc_gravity") g_mc_gravity.store(std::clamp(value, 5.f, 80.f));
                else if (key == "mc_jump_speed") g_mc_jump_speed.store(std::clamp(value, 1.f, 20.f));
                else if (key == "blocks") g_blocks_enabled.store(value != 0.f);
                else if (key == "block_drive") g_block_drive.store(value != 0.f);
                else if (key == "instant_turn") g_instant_turn.store(value != 0.f);
                else if (key == "turn_speed") g_turn_speed.store(std::clamp(value, 0.5f, 50.f));
                else if (key == "block_speed" || key == "walk_speed") g_block_walk_speed.store(std::clamp(value, 0.5f, 15.f));
                else if (key == "sneak_speed") g_sneak_speed.store(std::clamp(value, 0.2f, 10.f));
                else if (key == "sprint_speed") g_sprint_speed.store(std::clamp(value, 0.5f, 20.f));
                else if (key == "sprint_boost") g_sprint_boost.store(std::clamp(value, 0.0f, 20.f));
                else if (key == "sneak_key") g_sneak_vk.store(static_cast<int>(value));
                else if (key == "disable_vanilla_roll" || key == "disable_roll") g_disable_vanilla_roll.store(value != 0.f);
                else if (key == "roll_key") g_roll_vk.store(static_cast<int>(value));
                else if (key == "block_anchor") g_block_anchor.store(value != 0.f);
                else if (key == "block_ground") g_block_ground.store(value != 0.f);
                else if (key == "block_collision") g_block_collision.store(value != 0.f);
                else if (key == "enemy_block_collision" || key == "enemy_collision") g_enemy_block_collision.store(value != 0.f);
                else if (key == "reach") g_reach.store(std::clamp(value, 1.f, 8.f));
                else if (key == "no_stagger") g_no_stagger.store(value != 0.f);
                else if (key == "inv_key") g_inv_vk.store(static_cast<int>(value));
                else if (key == "inv_sens") g_inv_sens.store(std::max(0.1f, value));
                else if (key == "kb_force") g_kb_force.store(value);
                else if (key == "damage_scale") g_damage_scale.store(std::clamp(value, 0.0f, 200.0f));
                else if (key == "damage_hp_percent") g_damage_hp_percent.store(std::clamp(value, 0.0f, 1.0f));
                else if (key == "sweep_radius") g_sweep_radius.store(std::clamp(value, 0.5f, 10.0f));
                else if (key == "hide_slots") g_hide_slots.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask1") g_hide_mask1.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
                else if (key == "hide_mask2") g_hide_mask2.store(static_cast<uint32_t>(strtoul(line.c_str() + eq + 1, nullptr, 0)));
            }
        }
        cfg.depth_probe = FileExists(g_game_dir + "mc_er_depthprobe.txt");
        erov::SetSteveConfig(cfg);
        eraudio::Init((g_game_dir + "mods\\mc_adapter\\sounds\\").c_str(), g_sound_volume.load(), &Log);
        {
            std::vector<uint8_t> atlas;
            unsigned aw = 0, ah = 0;
            const std::string atlas_path_sub = g_game_dir + "mods\\mc_adapter\\mc_hud_atlas.png";
            const std::string atlas_path_root = g_game_dir + "mc_hud_atlas.png";
            const std::string atlas_path = FileExists(atlas_path_sub) ? atlas_path_sub : atlas_path_root;
            if (erov::DecodePngFile(atlas_path, atlas, aw, ah) && aw == mc::hud::kHudAtlasWidth && ah == mc::hud::kHudAtlasHeight) {
                erov::SetHudAtlas(atlas.data(), aw, ah);
                Log("hud atlas: external file %s (%ux%u)", atlas_path.c_str(), aw, ah);
            } else {
                Log("hud atlas: %s missing or not %ux%u (regenerate it with tools/extract_mc_assets.py), the HUD stays plain rectangles", atlas_path.c_str(),
                    static_cast<unsigned>(mc::hud::kHudAtlasWidth), static_cast<unsigned>(mc::hud::kHudAtlasHeight));
            }
        }
        {
            std::vector<uint8_t> skin;
            unsigned sw = 0, sh = 0;
            const std::string skin_path = g_game_dir + "mods\\mc_adapter\\steve.png";
            if (erov::DecodePngFile(skin_path, skin, sw, sh) && sw == 64 && sh == 64) {
                erov::SetSteveSkin(skin.data(), sw, sh);
                Log("skin: external file %s (%ux%u)", skin_path.c_str(), sw, sh);
            } else {
                Log("skin: %s missing or not 64x64, the figure stays flat grey-brown (extract it with tools/extract_mc_assets.py --export-steve-skin)", skin_path.c_str());
            }
        }
        {   // Load all available mob models and skins (creeper, zombie, skeleton)
            const std::vector<std::string> supported_mobs = {"creeper", "zombie", "skeleton"};
            for (const auto& mob : supported_mobs) {
                std::string model_path = g_game_dir + "mods\\mc_adapter\\models\\" + mob + ".geo.json";
                std::vector<uint8_t> json;
                if (ReadFileAll(model_path, json)) {
                    auto model = mc::model::EntityModel::fromJson(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()));
                    if (model && !model->bones.empty()) {
                        erov::SetMobModel(mob, *model);
                        Log("mobs: model %s (%s): %zu bones", mob.c_str(), model->identifier.c_str(), model->bones.size());
                    }
                } else {
                    Log("mobs: model file missing at %s", model_path.c_str());
                }
                std::string skin_path = g_game_dir + "mods\\mc_adapter\\" + mob + ".png";
                std::vector<uint8_t> mob_skin;
                unsigned zw = 0, zh = 0;
                if (erov::DecodePngFile(skin_path, mob_skin, zw, zh) && zw == 64 && (zh == 64 || zh == 32)) {
                    erov::SetMobSkin(mob, mob_skin.data(), zw, zh);
                    Log("mobs: skin %s (%ux%u)", mob.c_str(), zw, zh);
                } else {
                    Log("mobs: skin %s missing or not 64x64/64x32 at %s", mob.c_str(), skin_path.c_str());
                }
            }
        }
        g_steve_enabled.store(true);
        Log("steve: hide_native=%d mask1=0x%X mask2=0x%X slots=0x%X", g_hide_native.load() ? 1 : 0, g_hide_mask1.load(), g_hide_mask2.load(),
        g_hide_slots.load());
        Log("steve: enabled (occlusion=%d depth_const=%.4f rel_bias=%.3f abs_bias=%.3f scene_height=%.0f yaw_offset=%.1f deg)",
            cfg.occlusion ? 1 : 0, cfg.depth_const, cfg.rel_bias, cfg.abs_bias, cfg.scene_height, g_steve_yaw_offset * 180.f / 3.14159265f);
    }
    erov::Install(&HudProvider, [](const char* fmt, ...) {
        char msg[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
        Log("%s", msg);
    });
}

void SetupLocomotion() {
    eldenring::movement::LocomotionCallbacks cb;
    cb.get_player_chr = PlayerChrPtr;
    cb.get_physics_module = [](uintptr_t chr) { return detail::readPhysicsModule(g_reader, g_img.base, chr); };
    cb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return SafeCopy(addr, dst, sz); };
    cb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return WriteBytesSafe(addr, src, sz); };
    cb.is_game_in_foreground = GameInForeground;
    cb.want_suppress = WantSuppress;
    cb.is_player_dead = PlayerDead;
    cb.is_player_airborne = PlayerAirborne;
    cb.read_camera = [](CameraPose& out_cam) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        return world != 0 && readCamera(g_reader, g_img.base, world, out_cam);
    };
    cb.cast_ray = [](const float from[3], const float disp[3]) { return CastStaticRay(from, disp); };
    cb.is_supported_by_block = [](const float pos[3]) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        return g_blocks.count() != 0 && blocks::supportedByBlock(g_blocks, pos);
    };
    cb.resolve_block = [](const float pos[3]) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_blocks.count() != 0) return blocks::resolvePlayer(g_blocks, pos);
        blocks::Resolve r;
        r.moved = false;
        r.feet[0] = pos[0]; r.feet[1] = pos[1]; r.feet[2] = pos[2];
        return r;
    };
    cb.resolve_block_swept = [](const float prev[3], const float curr[3]) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_blocks.count() != 0) return blocks::resolvePlayerSwept(g_blocks, prev, curr);
        blocks::Resolve r;
        r.moved = false;
        r.feet[0] = curr[0]; r.feet[1] = curr[1]; r.feet[2] = curr[2];
        return r;
    };
    cb.clamp_sneak = [](const float feet[3], float dx, float dz) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        return blocks::clampSneakEdgeMovement(g_blocks, feet, dx, dz);
    };
    cb.check_sprint_collision = [](float target_x, float feet_y, float target_z) {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        const auto c_low = blocks::cellOf(target_x, feet_y + 0.1f, target_z);
        const auto c_high = blocks::cellOf(target_x, feet_y + 1.1f, target_z);
        return (g_blocks.get(c_low) != mc::BlockId::Air || g_blocks.get(c_high) != mc::BlockId::Air);
    };
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    eldenring::movement::PlayerLocomotion::Instance().setCallbacks(cb);
}

void SetupWorldRuntime() {
    eldenring::world::WorldRuntimeCallbacks cb;
    cb.get_player_chr = PlayerChrPtr;
    cb.get_physics_module = [](uintptr_t chr) { return detail::readPhysicsModule(g_reader, g_img.base, chr); };
    cb.read_physics_pos = [](uintptr_t chr, float out_pos[3]) { return detail::readPhysicsPosition(g_reader, g_img.base, chr, out_pos); };
    cb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return SafeCopy(addr, dst, sz); };
    cb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return WriteBytesSafe(addr, src, sz); };
    cb.is_game_in_foreground = GameInForeground;
    cb.read_camera = [](CameraPose& out_cam) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        return world != 0 && readCamera(g_reader, g_img.base, world, out_cam);
    };
    cb.cast_ray = [](const float from[3], const float disp[3]) { return CastStaticRay(from, disp); };
    cb.enumerate_enemies = [](std::vector<EnemyInfo>& out) { return enumerateEnemies(g_reader, g_img.base, out, 512); };
    cb.apply_damage_to_chr = [](uintptr_t chr, int dmg) {
        g_queue.enqueue(chr, dmg, NowTick(), 4u | (static_cast<uint32_t>(std::min(255, dmg / 10)) << 8));
    };
    cb.play_sound = [](const char* snd, float vol) { eraudio::Play(snd, vol); };
    cb.spawn_fx = [](int kind, const float pos[3], int count) { erov::SpawnFx(static_cast<erov::FxKind>(kind), pos, count); };
    cb.drive_velocity = [](uintptr_t module, bool pinned, eldenring::RuntimeContext& ctx) {
        eldenring::movement::PlayerLocomotion::Instance().driveVelocity(module, pinned, ctx);
    };
    cb.reset_fall_timer = [](uintptr_t chr) {
        eldenring::movement::PlayerLocomotion::Instance().resetFallTimer(chr);
    };
    cb.unlock_air_movement = [](uintptr_t chr) {
        eldenring::movement::PlayerLocomotion::Instance().unlockAirMovement(chr);
    };
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    cb.on_origin_rebase = [](float rx, float ry, float rz) {
        {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            g_tnt_mgr.shift(rx, ry, rz);
        }
        g_virtual_summons.shift(rx, ry, rz);
        SendBlockMesh();
    };
    eldenring::world::VoxelWorldRuntime::Instance().setCallbacks(cb);

    g_virtual_summons.setGroundSampler([](float x, float y, float z) -> std::optional<float> {
        float feet[3] = {x, y, z};
        const auto cast = [](const float from[3], const float disp[3]) {
            return CastStaticRay(from, disp);
        };
        const auto block_top = [](float bx, float by, float bz, float max_drop) -> std::optional<float> {
            std::lock_guard<std::mutex> g(g_blocks_mutex);
            if (g_blocks.count() == 0) return std::nullopt;
            const int top_cell = static_cast<int>(std::floor(by + 0.06f));
            const int low_cell = static_cast<int>(std::floor(by - max_drop));
            for (int cy = top_cell; cy >= low_cell; --cy) {
                if (g_blocks.get(blocks::cellOf(bx, static_cast<float>(cy), bz)) != mc::BlockId::Air) {
                    return static_cast<float>(cy + 1);
                }
            }
            return std::nullopt;
        };
        return eldenring::shadow::groundBelow(cast, block_top, feet);
    });
}

void SetupCombat() {
    eldenring::combat::CombatCallbacks cb;
    cb.get_player_chr = PlayerChrPtr;
    cb.read_physics_pos = [](uintptr_t chr, float feet[3]) { return detail::readPhysicsPosition(g_reader, g_img.base, chr, feet); };
    cb.read_physics_orient = [](uintptr_t chr, float quat[4]) { return detail::readPhysicsOrientation(g_reader, g_img.base, chr, quat); };
    cb.read_camera = [](CameraPose& out_cam) {
        const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
        return world != 0 && readCamera(g_reader, g_img.base, world, out_cam);
    };
    cb.safe_read = [](uintptr_t addr, void* dst, size_t sz) { return SafeCopy(addr, dst, sz); };
    cb.safe_write = [](uintptr_t addr, const void* src, size_t sz) { return WriteBytesSafe(addr, src, sz); };
    cb.has_line_of_sight = PlayerCanSee;
    cb.read_vitals = [](uintptr_t chr, Vitals& out_v) { return readVitals(g_reader, g_img.base, chr, out_v); };
    cb.set_chr_hp = [](uintptr_t chr, int new_hp) { SetChrHp(chr, new_hp); };
    cb.call_vfunc7 = [](uintptr_t dmg_module, uintptr_t attacker, void* ctx_ptr) { return CallVfunc7(dmg_module, attacker, ctx_ptr); };
    cb.play_sound = [](const char* snd, float vol) { eraudio::Play(snd, vol); };
    cb.spawn_fx = [](int kind, const float pos[3], int count) { erov::SpawnFx(static_cast<erov::FxKind>(kind), pos, count); };
    cb.spawn_tracer = [](const float from[3], const float to[3], float life) { erov::SpawnTracer(from, to, life); };
#if ENABLE_CS2_VIEWMODEL
    cb.spawn_decal = [](const float pos[3], const float normal[3]) { erov::SpawnDecal(pos, normal); };
#endif
    cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
    cb.reader = &g_reader;
    cb.image_base = g_img.base;
    cb.get_game_tid = []() -> uint32_t { return g_game_tid.load(std::memory_order_relaxed); };
    cb.spawn_bullet = [](void* manager, uint32_t* out_handle, const void* req, void* status) -> bool {
        return SpawnBulletSafe(manager, out_handle, const_cast<void*>(req), status);
    };
    cb.override_final_damage = [](uint8_t* ctx, uintptr_t atk, uintptr_t vic, int val, int* eng_val) -> bool {
        return overrideFinalDamage(ctx, atk, vic, val, eng_val);
    };
    cb.is_falling = PlayerAirborne;
    cb.file_exists = [](const std::string& path) -> bool {
        return FileExists(g_game_dir + path);
    };
    eldenring::combat::CombatService::Instance().setCallbacks(cb);
}

void SetupInput() {
    SetupLocomotion();
    SetupWorldRuntime();
    SetupCombat();
    {
        eldenring::ui::InventoryCallbacks cb;
        cb.log = [](const char* msg) { Log("%s", msg); };
        eldenring::ui::InventoryController::Instance().setCallbacks(cb);
    }
    {
        eldenring::input::HotkeyCallbacks cb;
        cb.is_game_in_foreground = []() -> bool {
            const bool fg = GameInForeground();
            static int prev_fg = -1;
            static uint64_t last_fail_log = 0;
            if (static_cast<int>(fg) != prev_fg) {
                prev_fg = static_cast<int>(fg);
                HWND top = GetForegroundWindow();
                char cls[80] = {}, title[120] = {};
                DWORD pid = 0;
                if (top != nullptr) {
                    GetClassNameA(top, cls, sizeof(cls) - 1);
                    GetWindowTextA(top, title, sizeof(title) - 1);
                    GetWindowThreadProcessId(top, &pid);
                }
                Log("focus: the game is %s the foreground window (foreground: hwnd=%p pid=%lu class='%s' title='%s')",
                    fg ? "now" : "no longer", reinterpret_cast<void*>(top),
                    static_cast<unsigned long>(pid), cls, title);
            }
            long hr = 0;
            const unsigned failed = erin::TakeKeyboardFailures(hr);
            const uint64_t t = GetTickCount64();
            if (failed > 0 && t - last_fail_log >= 1000) {
                last_fail_log = t;
                Log("input: %u keyboard reads failed in the last second, last error 0x%08lX",
                    failed, static_cast<unsigned long>(hr));
            }
            return fg;
        };
        cb.want_suppress = WantSuppress;
        cb.is_player_dead = PlayerDead;
        cb.is_player_airborne = PlayerAirborne;
        cb.get_player_chr = PlayerChrPtr;
        cb.read_physics_pos = [](uintptr_t chr, float out_pos[3]) {
            return detail::readPhysicsPosition(g_reader, g_img.base, chr, out_pos);
        };
        cb.play_sound = [](const char* s, float v) { eraudio::Play(s, v); };
        cb.log = [](const std::string& msg) { Log("%s", msg.c_str()); };
        cb.clip_duration = [](const char* n) { return erov::CsClipDuration(n); };
        cb.take_left_click = erin::TakeLeftClick;
        cb.take_right_click = erin::TakeRightClick;
        cb.left_held = erin::LeftHeld;
        cb.right_held = erin::RightHeld;
        cb.take_wheel_notches = erin::TakeWheelNotches;
        cb.set_suppress_mouse_buttons = erin::SetSuppressMouseButtons;
        cb.set_suppress_mouse_wheel = erin::SetSuppressMouseWheel;
        cb.set_suppress_mouse_motion = erin::SetSuppressMouseMotion;
        cb.set_suppress_keyboard = erin::SetSuppressKeyboard;
        cb.set_masked_keys = erin::SetMaskedKeys;
        cb.take_keyboard_failures = erin::TakeKeyboardFailures;
        cb.step_mining = [](float dt, bool left_held) { return StepMining(dt, left_held); };
        cb.try_ignite_tnt = TryIgniteTnt;
        cb.try_ignite_creeper = TryIgniteCreeper;
        cb.try_place_block = TryPlaceBlock;
        cb.try_use_spawn_egg = TryUseSpawnEgg;
        cb.click_attack = ClickAttack;
        cb.enqueue_nearest_hostile = EnqueueNearestHostile;
        cb.queue_bullet_fire = [](float dmg) { return QueueBulletFire(dmg); };
        cb.queue_cs_shot = [](float damage, float spread, float range, uint32_t tracer, uint8_t slot) { return FireCsShot(damage, spread, range, tracer, slot); };
        cb.queue_summon = [](const char* src) { return QueueSummon(src); };
        cb.cycle_hidden_slot = [](int& cur) { CycleHiddenSlot(cur); };
        cb.reset_slot_probe = [](int& cur) { ResetSlotProbe(cur); };
        cb.snapshot_native_parts = SnapshotNativeParts;
        cb.request_frame_trace = [](int f) { erov::RequestFrameTrace(f); };
        cb.request_screenshot = [](const char* p) { erov::RequestScreenshot(p); };
        cb.cycle_depth_candidate = erov::CycleDepthCandidate;
        cb.inventory_tick = InventoryTick;
        cb.blocks_tick = []() {
            BlocksTick();
            if (g_mc_mode.load()) LogGroundBytesOnChange();
            if (g_mc_mode.load() && FileExists(g_game_dir + "mc_er_physlog.txt")) LogPhysicsVectors();
        };
        cb.mc_flags_step = McFlagsStep;
        cb.check_summon_watch = [](uint64_t) {
            SummonWatch& summon_watch = g_summon_watch;
            if (summon_watch.reports_left > 0) {
                const uint64_t waited = GetTickCount64() - summon_watch.t0_ms;
                if (waited >= (summon_watch.reports_left == 2 ? 1500u : 5000u)) {
                    --summon_watch.reports_left;
                    std::vector<EnemyInfo> now_list;
                    if (enumerateEnemies(g_reader, g_img.base, now_list, 4000)) {
                        std::vector<uintptr_t> ptrs;
                        for (const EnemyInfo& e : now_list) ptrs.push_back(e.chr);
                        const auto fresh = eldenring::buddy::newEntities(summon_watch.baseline, ptrs);
                        Log("summon: %.1f s after F2, %zu new entities (of %zu)", static_cast<double>(waited) / 1000.0, fresh.size(), now_list.size());
                        for (size_t i : fresh) {
                            const EnemyInfo& e = now_list[i];
                            Log("summon:   new chr=%p npc=%d team=%u hp=%d/%d hostile=%d rel=(%.1f %.1f %.1f) dist=%.1f m", reinterpret_cast<void*>(e.chr),
                                e.npc_id, static_cast<unsigned>(e.team), e.hp, e.max_hp, e.hostile ? 1 : 0, e.rel_x, e.rel_y, e.rel_z,
                                std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z));
                            if (summon_watch.reports_left == 0 && FileExists(g_game_dir + "mc_er_chrscan.txt")) LogChrScan(e.chr, i);
                        }
                    } else {
                        Log("summon: entity enumeration failed");
                    }
                }
            }
        };
        eldenring::input::HotkeyDispatcher::Instance().setCallbacks(cb);
        Log("hotkey_dispatcher: initialized and bound to native game subsystems");
    }
    if (!FileExists(g_game_dir + "mc_er_input.txt")) {
        Log("input: disabled (no mc_er_input.txt)");
        return;
    }
    g_input_enabled.store(true);
    g_click_attack.store(true);
    Log("input: mouse buttons are cleared at the DirectInput layer while MC mode is on and no game UI is open; left click attacks");
}

// ---- status loop ---------------------------------------------------------------------------------------------

void LogStatus() {
    const uintptr_t world = readSingleton(g_reader, g_img.base, g_rva_world, sigs::kWorldChrMan);
    const uintptr_t menu = readSingleton(g_reader, g_img.base, g_rva_menu, sigs::kCSMenuMan);
    const uintptr_t loading = readSingleton(g_reader, g_img.base, g_rva_loading, sigs::kCSNowLoadingHelper);
    const uintptr_t fade = readSingleton(g_reader, g_img.base, g_rva_fade, sigs::kCSFade);
    char line[512];
    int n = snprintf(line, sizeof(line), "world=%p", reinterpret_cast<void*>(world));
    Vitals v;
    if (world != 0) {
        uint64_t player = 0;
        SafeCopy(world + layout::kPlayerInsInWorldChrMan, &player, sizeof(player));
        if (player != 0 && readVitals(g_reader, g_img.base, static_cast<uintptr_t>(player), v)) {
            n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " hp=%d/%d", v.hp, v.max_hp);
        }
    }
    MenuState ms;
    if (readMenuState(g_reader, menu, ms)) {
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " menu[a=0x%02X b=0x%02X popup=%d]", ms.layer_mask_a,
                      ms.layer_mask_b, ms.popup_open ? 1 : 0);
    }
    LoadingState ls;
    if (readLoadingState(g_reader, loading, ls)) {
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " load[job=%d screen=%d cnt=%d]", ls.job_active ? 1 : 0,
                      ls.screen_loading ? 1 : 0, ls.load_counter);
    }
    float fa = 0.f;
    if (readFadeAlpha(g_reader, fade, fa)) n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " fade=%.2f", fa);
    std::vector<EnemyInfo> list;
    if (world != 0 && enumerateEnemies(g_reader, g_img.base, list, 2000)) {
        int hostile = 0;
        float nearest = 1e9f;
        for (const EnemyInfo& e : list) {
            if (!e.hostile) continue;
            ++hostile;
            nearest = std::min(nearest, std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z));
        }
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " enemies=%zu hostile=%d nearest=%.1f", list.size(), hostile,
                      hostile > 0 ? nearest : -1.f);
    }
    if (g_input_enabled.load()) {
        const erin::Counters c = erin::TakeCounters();
        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " input[mouse state=%u data=%u kbd state=%u data=%u other=%u cleared=%u]",
                      c.mouse_state, c.mouse_data, c.keyboard_state, c.keyboard_data, c.other, c.mouse_buttons_cleared);
    }
    (void)n;
    Log("%s", line);
}

DWORD WINAPI LoaderThread(LPVOID) {
    InitLogOnce();

    // Environment self-check: never touch a game that runs EasyAntiCheat.
    std::vector<std::string> names;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const size_t count = std::min<size_t>(needed / sizeof(HMODULE), 1024);
        for (size_t i = 0; i < count; ++i) {
            char name[MAX_PATH] = {};
            if (GetModuleFileNameA(mods[i], name, MAX_PATH)) {
                std::string s = name;
                names.push_back(s.substr(s.find_last_of("\\/") + 1));
            }
        }
    }
    const EnvVerdict verdict = checkEnvironment(names, FileExists(g_game_dir + "steam_appid.txt"));
    if (verdict != EnvVerdict::Ok) {
        Log("environment check FAILED (%s): not binding anything",
            verdict == EnvVerdict::EacLoaded ? "EAC module loaded" : "steam_appid.txt missing");
        return 0;
    }
    Log("environment check ok (%zu modules)", names.size());

    // Bind the singletons: wait for the code to be readable and the signatures to be unique.
    for (int attempt = 0;; ++attempt) {
        if (ReadImageInfo(g_img)) {
            const Bound w = LocateSingleton(g_img, sigs::kWorldChrMan);
            const Bound m = LocateSingleton(g_img, sigs::kCSMenuMan);
            const Bound l = LocateSingleton(g_img, sigs::kCSNowLoadingHelper);
            const Bound f = LocateSingleton(g_img, sigs::kCSFade);
            if (w.ok && m.ok && l.ok && f.ok) {
                g_rva_world = w.rva;
                g_rva_menu = m.rva;
                g_rva_loading = l.rva;
                g_rva_fade = f.rva;
                Log("singletons bound: WorldChrMan=0x%X CSMenuMan=0x%X CSNowLoadingHelper=0x%X CSFade=0x%X (image base %p)", w.rva,
                    m.rva, l.rva, f.rva, reinterpret_cast<void*>(g_img.base));
                break;
            }
            if (attempt % 5 == 0) {
                Log("waiting for signatures (world=%d menu=%d loading=%d fade=%d)", w.ok, m.ok, l.ok, f.ok);
            }
        }
        if (attempt > 120) {
            Log("giving up: singleton signatures never became unique");
            return 0;
        }
        Sleep(1000);
    }

    SetupDamage();
    SetupCreativeProbes();
    SetupBulletLog();
    SetupOverlay();
    SetupInput();
    CreateThread(nullptr, 0, KeyThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, BuddyThread, nullptr, 0, nullptr);

    for (;;) {
        Sleep(1000);
        if (g_game_tid.load() == 0) {
            const DWORD tid = FindGameWindowThread();
            if (tid != 0) {
                g_game_tid.store(tid);
                Log("game thread = %lu (owner of the game window)", tid);
            }
        }
        LogStatus();
    }
}

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInput8Create_t g_system_create = nullptr;
std::atomic<bool> g_wants_input_hooks{false};

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    if (g_system_create == nullptr) {
        char dir[MAX_PATH] = {};
        if (GetSystemDirectoryA(dir, MAX_PATH) > 0) {
            const std::string path = std::string(dir) + "\\dinput8.dll";
            if (HMODULE m = LoadLibraryA(path.c_str())) {
                g_system_create = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(m, "DirectInput8Create"));
            }
        }
    }
    if (g_system_create == nullptr) return E_FAIL;
    static std::once_flag decided;
    std::call_once(decided, [] {
        InitLogOnce();
        const DWORD a = GetFileAttributesA((g_game_dir + "mc_er_input.txt").c_str());
        if (a != INVALID_FILE_ATTRIBUTES && EnsureMinHook()) {
            erin::SetLog([](const char* fmt, ...) {
                char msg[512];
                va_list args;
                va_start(args, fmt);
                vsnprintf(msg, sizeof(msg), fmt, args);
                va_end(args);
                Log("%s", msg);
            });
            g_wants_input_hooks.store(true);
        }
    });
    const HRESULT hr = g_system_create(hinst, version, riid, out, outer);
    if (SUCCEEDED(hr) && out != nullptr && *out != nullptr && g_wants_input_hooks.load()) erin::OnDirectInputCreated(riid, *out);
    return hr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
