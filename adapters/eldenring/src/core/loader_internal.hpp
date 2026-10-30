// Internal interface shared by the Elden Ring loader translation units.
//
// Issue #96: loader.cpp is only the dinput8.dll entry point. The gameplay
// wiring lives in the loader_detail modules; this header is the single place
// where their shared state and entry points are declared. It is adapter-only
// (Windows), never included by mc_tests.
#pragma once

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
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <MinHook.h>

#include "mc/contracts/combat_adapter.hpp"
#include "mc/er_region_table.hpp"
#include "mc/zone_detector.hpp"

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
#include "mc/ground_mining.hpp"
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

namespace eldenring::loader_detail {

using namespace eldenring::live;

namespace blocks = eldenring::blocks;
namespace creative = eldenring::creative;
namespace flight = eldenring::flight;

using eldenring::diagnostics::Log;
using eldenring::diagnostics::InitLogOnce;
using eldenring::diagnostics::CrashVehHandler;
using eldenring::debug::BuddyThread;
using eldenring::debug::CamWatchThread;

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

using ApplyHpFn = void*(__fastcall*)(void* data_module, int32_t hp, uint8_t flag);
using DamageFn = uint64_t(__fastcall*)(void*, void*, void*);
using HitCollectorFn = void(__fastcall*)(void* self, const void* hit);
using MaterialLookupFn = bool(__fastcall*)(const uint32_t* shape_key, uint32_t material_index, uint32_t* out_material, bool resolve);
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using HiddenWord = eldenring::debug::HiddenWord;

struct CamKeep {
    uintptr_t pos_addr{0};
    float third_person[3]{};
    bool valid{false};
};

struct BlockTarget {
    bool have{false};
    bool on_block{false};        // the nearest surface is a placed block (else the game's terrain)
    bool terrain_hit{false};     // the game's static geometry was hit (ground mining)
    bool aim_ok{false};          // the camera ray could be read
    blocks::Cell cell;           // the placed block, when on_block
    blocks::Cell place;          // where a new block would go
    float eye[3]{0.f, 0.f, 0.f};
    float dir[3]{0.f, 0.f, 1.f};
    float pitch_deg{0.f};
    float terrain_pos[3]{0.f, 0.f, 0.f};
    float terrain_normal[3]{0.f, 1.f, 0.f};
};

using eldenring::combat::ShieldVerdict;

struct SummonWatch {
    std::vector<uintptr_t> baseline;
    uint64_t t0_ms{0};
    int reports_left{0};
};

// ---- process / image state -----------------------------------------------------------------
extern const std::string& g_game_dir;
extern const SelfReader g_reader;
extern ImageInfo g_img;
extern uint32_t g_rva_world, g_rva_menu, g_rva_loading, g_rva_fade;
extern std::atomic<DWORD> g_game_tid;

// ---- RuntimeContext and its sub-systems ----------------------------------------------------
extern RuntimeContext& g_ctx;
extern ModeConfig& g_mode;
extern PlayerSystems& g_player;
extern WorldSystems& g_world;
extern CombatSystems& g_combat;
extern UiSystems& g_ui;

// Reference aliases kept from the pre-split loader so the moved code reads unchanged.
extern std::atomic<bool>& g_damage_enabled;
extern std::atomic<bool>& g_mc_mode;
extern std::atomic<uint8_t>& g_game_mode;
extern std::atomic<uint64_t>& g_mode_toast_ms;
extern std::atomic<bool>& g_input_enabled;
extern std::atomic<bool>& g_click_attack;
extern std::atomic<bool>& g_steve_enabled;
extern std::atomic<bool>& g_hide_native;
extern std::atomic<bool>& g_native_hide_wanted;
extern std::atomic<unsigned>& g_native_reshown;
extern std::atomic<bool>& g_slots_changed;
extern std::atomic<int>& g_slot_probe;
extern std::atomic<uint32_t>& g_hide_mask1;
extern std::atomic<bool>& g_no_player_hit_vfx;
extern std::atomic<bool>& g_fp_persist;
extern std::atomic<bool>& g_shadow_enabled;
extern std::atomic<float>& g_ground_y;
extern std::atomic<bool>& g_ground_valid;
extern std::atomic<bool>& g_first_person;
extern std::atomic<float>& g_eye_height;
extern std::atomic<float>& g_kb_force;
extern std::atomic<uint32_t>& g_hide_slots;
extern std::atomic<uint32_t>& g_hide_mask2;
extern std::atomic<bool>& g_instant_turn;
extern std::atomic<float>& g_turn_speed;
extern std::atomic<bool>& g_disable_vanilla_roll;
extern std::atomic<int>& g_roll_vk;
extern std::atomic<bool>& g_require_victim_updating;
extern std::atomic<float>& g_damage_scale;
extern std::atomic<float>& g_damage_hp_percent;
extern std::atomic<float>& g_sweep_radius;
extern DamageQueue& g_queue;
extern std::mutex& g_melee_mutex;
extern MeleeController& g_melee;
extern JumpTracker& g_jump;
extern eldenring::sprint::SprintController& g_sprint;
extern std::atomic<bool>& g_sprint_active;
extern std::atomic<float>& g_sprint_fov;
extern std::atomic<float>& g_sprint_anim;
extern SurvivalState& g_survival;
extern HungerModel& g_hunger;
extern XpModel& g_xp;
extern float& g_hunger_heal_carry;
extern mc::ConsumableSystem& g_eating;
extern std::atomic<float>& g_sound_volume;
extern std::atomic<float>& g_walk_speed;
extern std::atomic<bool>& g_on_ground;
extern std::atomic<bool>& g_bullet_fire_pending;
extern std::atomic<bool>& g_bullet_fire_enabled;
extern std::atomic<bool>& g_summon_pending;
extern std::atomic<int>& g_heal_pending;
extern HitFeedback& g_feedback;
extern std::optional<HitTemplate>& g_template;
extern std::atomic<bool>& g_inv_open;
extern std::atomic<float>& g_inv_mx;
extern std::atomic<float>& g_inv_my;
extern std::atomic<int>& g_inv_vk;
extern std::atomic<float>& g_inv_sens;
extern std::atomic<unsigned>& g_isblocked_forced;
extern std::atomic<unsigned>& g_menufreeze_forced;
extern std::atomic<bool>& g_shield_blocking;
extern std::atomic<bool>& g_shield_offhand;
extern std::atomic<bool>& g_bow_offhand;
extern std::atomic<float>& g_bow_power;
extern std::atomic<float>& g_bow_ticks;
extern blocks::BlockGrid& g_blocks;
extern eldenring::tnt::ErTntManager& g_tnt_mgr;
extern std::mutex& g_blocks_mutex;
extern std::atomic<bool>& g_blocks_enabled;
extern std::atomic<float>& g_block_walk_speed;
extern std::atomic<float>& g_sneak_speed;
extern std::atomic<float>& g_sprint_speed;
extern std::atomic<int>& g_sneak_vk;
extern std::atomic<bool>& g_sneak_active;
extern std::atomic<bool>& g_block_drive;
extern std::atomic<bool>& g_block_anchor;
extern std::atomic<bool>& g_block_ground;
extern std::atomic<bool>& g_block_collision;
extern std::atomic<bool>& g_enemy_block_collision;
extern std::atomic<bool>& g_block_supported;
extern std::atomic<float>& g_reach;

extern float g_steve_yaw_offset;
extern std::unordered_map<uintptr_t, HiddenWord> g_hidden_flags;
extern std::mutex g_native_mutex;

// ---- loose flags (not yet in RuntimeContext; kept here for the code motion) ----------------
extern std::atomic<bool> g_no_stagger;
extern std::atomic<unsigned> g_stagger_skipped;
extern std::atomic<bool> g_fall_protect;
extern std::atomic<uint64_t> g_movement_layer_ms;
extern std::atomic<unsigned> g_kills_skipped;
extern std::mutex g_cr_mutex;
extern creative::FallLogState g_cr_fall_state;
extern std::atomic<bool> g_cr_log;
extern std::atomic<bool> g_summon_hide;
extern CamKeep g_cam_keep;
extern std::atomic<bool> g_camstep_installed;
extern std::atomic<unsigned> g_camstep_calls, g_camstep_changed;
extern std::mutex g_summon_mutex;
extern std::vector<eldenring::mobs::MobSnapshot> g_summon_list;
extern std::vector<eldenring::summon::ArrowDraw> g_summon_arrows;
extern float g_mob_yaw_offset;
extern eldenring::summon::VirtualSummonManager g_virtual_summons;
extern std::set<uintptr_t> g_hidden_flag_addresses;
extern std::map<uintptr_t, uint16_t> g_suppressed_sfx_slots;
extern std::atomic<unsigned> g_hitvfx_skipped;
extern std::atomic<bool> g_los_enabled;
extern std::atomic<unsigned> g_los_blocked;
extern std::atomic<bool> g_ground_mining_enabled;
extern std::atomic<int> g_loot_zone;
extern std::atomic<float> g_ground_crack_lift;
extern std::atomic<float> g_ground_crack_size;
extern std::vector<mc::ZoneRule> g_zone_material_rules;
extern std::vector<mc::ZoneRule> g_zone_area_rules;
extern mc::ZoneRules g_zone_rules;
extern std::atomic<bool> g_zone_probe_log;
extern std::atomic<uint32_t> g_last_ground_material;
extern std::atomic<uint32_t> g_last_ground_shape;
extern std::atomic<uint64_t> g_zone_probe_last_log_ms;
extern HitCollectorFn g_hit_collector_orig;
extern MaterialLookupFn g_material_lookup;
extern std::atomic<bool> g_pdc_hooked;
extern std::atomic<bool> g_log_player_hits;
extern std::atomic<bool> g_proj_log;
extern std::atomic<bool> g_arrow_damage;
extern std::atomic<int> g_proj_log_left;
extern std::atomic<uint64_t> g_guard_sound_ms;
extern std::atomic<bool> g_knockdown_fix;
extern std::atomic<bool> g_sneak_clamped_x;
extern std::atomic<bool> g_sneak_clamped_z;
extern unsigned& g_blocks_sent_version;
extern SummonWatch g_summon_watch;
extern std::atomic<uint64_t> g_last_summon_ms;
extern std::atomic<bool> g_fall_reset;
extern std::atomic<float> g_fall_hold;
extern std::atomic<float> g_pre_fall_t;
extern std::atomic<float> g_sprint_boost;
extern std::atomic<bool> g_mc_jump;
extern std::atomic<int> g_mc_jump_vk;
extern std::atomic<float> g_mc_jump_speed;
extern std::atomic<float> g_mc_gravity;
extern std::atomic<bool> g_flying;
extern std::atomic<float> g_fly_h_speed;
extern std::atomic<float> g_fly_v_speed;
extern std::atomic<float> g_fly_yaw;
extern std::atomic<bool> g_fly_sync;
extern std::atomic<unsigned> g_bullet_seen;
extern std::atomic<uint64_t> g_last_bullet_fire_ms;
extern ApplyHpFn g_apply_hp;
extern thread_local int32_t t_hp_after;
extern DirectInput8Create_t g_system_create;
extern std::atomic<bool> g_wants_input_hooks;
extern mc::CombatEngine g_combat_engine;

// ---- entry points --------------------------------------------------------------------------
creative::GameMode CurrentGameMode();
std::span<const mc::ZoneRule> DefaultZoneAreaRules();
DWORD WINAPI LoaderThread(LPVOID);
HRESULT WINAPI DirectInput8CreateImpl(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer);
DWORD WINAPI KeyThread(LPVOID);
DWORD WINAPI CrackTunerThread(LPVOID);

// ---- services ------------------------------------------------------------------------------
DWORD FindGameWindowThread();
bool GameInForeground();
bool EnsureMinHook();
bool FileExists(const std::string& path);
bool ReadFileAll(const std::string& path, std::vector<uint8_t>& out);
void LogStatus();

// ---- player / memory helpers ---------------------------------------------------------------
uint64_t NowTick();
bool CallVfunc7(uintptr_t damage_module, uintptr_t attacker, void* ctx);
void ReadHurt(float& hurt, float& side);
bool WriteBytesSafe(uintptr_t address, const void* src, size_t n);
uintptr_t ModuleOwner(void* fall_module);
bool IsPlayerActionModule(void* self);
bool CreativeShouldLogFall(float metres, uintptr_t caller_rva);
uintptr_t PlayerChrPtr();
bool PlayerDead();
bool PlayerAirborne();
uintptr_t DataModuleOfChr(uintptr_t chr);
bool GiveHp(uintptr_t data_module, int amount);
bool SetChrHp(uintptr_t chr, int32_t new_hp);
bool KillChr(uintptr_t chr);
int32_t HpOf(uintptr_t chr);
void LogHitGeometry(const char* tag, void* attacker, uintptr_t player, const uint8_t* ctx);
void LogPlayerHit(void* module, void* attacker, const uint8_t* ctx, uint8_t blocked_flag);
void LogChrScan(uintptr_t chr, size_t index);
void LogPhysicsVectors();
void LogPlayerPoseForBuddy();
void LogGroundBytesOnChange();
void SnapshotNativeParts();
void ResetSlotProbe(int& cursor);
void CycleHiddenSlot(int& cursor);
RayResult CastStaticRay(const float from[3], const float disp[3]);
bool PlayerCanSee(const float player_feet[3], const float victim_feet[3]);
const char* OutcomeName(DamageOutcome o);
bool WantSuppress();

// ---- combat --------------------------------------------------------------------------------
ShieldVerdict EvaluateShield(uintptr_t player, uintptr_t attacker_chr);
bool PlayerHitSurvival(void* module, void* attacker, uint8_t* ctx, uint8_t blocked, uint32_t& popped_heal_target);
void HandleProjectileHit(void* module, void* attacker, uint8_t* ctx);
void DrainOnce(uintptr_t updating_data_module);
void EnqueueNearestHostile();
bool ClickAttack(float charged);

// ---- bullets -------------------------------------------------------------------------------
bool QueueBulletFire(float mc_damage);
void RunBulletFire();
void AimCalTick();
void AimCalStart(uint64_t manager, uint32_t handle, const float dir[3]);
bool FireCsShot(float damage, float spread, float range, uint32_t tracer_frequency, uint8_t weapon_slot);
void LogBulletRequest(void* manager, const void* request, const void* r9, void* ret_addr, uint32_t handle);
void SaveBulletTemplate(const uint8_t* body);
void LoadBulletTemplate();
void RecordBulletTemplate(const void* request);
bool SpawnBulletSafe(void* manager, uint32_t* out_handle, void* request, void* ctx);
void SetupBulletLog();

// ---- world: summons ------------------------------------------------------------------------
void HideSummonChr(uintptr_t chr);
void HideSummonModels();
void VirtualSummonsStep(float dt);
bool QueueSummon(const char* source, const char* requested_mob = nullptr);
void RunSummonExperiment();

// ---- world: camera -------------------------------------------------------------------------
bool ChrCamPosAddress(uintptr_t& addr);
bool CameraPositionTargetSafe(uintptr_t addr);
void RestoreCamKeep();
void AfterCameraStep(bool have_before, uintptr_t addr, const float before[3]);

// ---- world: mining -------------------------------------------------------------------------
void __fastcall HitCollectorDetour(void* self, const void* hit);
uint32_t PlayerBlockId();
bool StepMining(float dt, bool left_held);

// ---- world: blocks / items -----------------------------------------------------------------
bool BlocksPresent();
void SendBlockMesh(const std::optional<blocks::MiningOverlay>& mining = std::nullopt,
                   const std::optional<blocks::TerrainMiningOverlay>& terrain_mining = std::nullopt);
bool AimRay(float eye[3], float dir[3]);
BlockTarget FindBlockTarget();
const char* BlockSound(mc::BlockId id);
bool TryUseSpawnEgg();
bool TryIgniteTnt();
bool TryIgniteCreeper();
bool TryPlaceBlock();

// ---- world: TNT / collision / flags --------------------------------------------------------
void ProcessTntExplosionsOnGameThread(const std::vector<EnemyInfo>& enemies);
void EnemiesBlockCollisionStep(const float player_feet[3]);
void TriggerTntExplosionEffectsAndDamage(const float center[3]);
void BlocksTick();
void McFlagsStep();

// ---- movement ------------------------------------------------------------------------------
void DriveVelocityFromKeys(uintptr_t module, bool force_zero);
uintptr_t FallModuleOf(uintptr_t chr);
uintptr_t ActionFlagModuleOf(uintptr_t chr);
void UnlockAirMovement(uintptr_t chr);
float FallTimer(uintptr_t chr);
void WriteFallTimer(uintptr_t chr, float value);
void ResetFallTimer(uintptr_t chr);
void LandFallTimer(uintptr_t chr);
void ShadowGroundStep();
void BlocksCollisionStep();
void GroundSprintStep(float dt);
void McJumpStep();
void FlightStep();
void InventoryTick(bool fg);

// ---- hook installation / domain wiring -----------------------------------------------------
void SetupZoneProbe();
void SetupCreativeProbes();
void SetupGameHooksCallbacks();
void SetupDamage();
void SetupLocomotion();
void SetupWorldRuntime();
void SetupCombat();
void SetupInput();

// ---- HUD / overlay -------------------------------------------------------------------------
void RestoreNativeModel();
void UpdateNativeModel(uintptr_t player, bool hide);
bool HudProvider(erov::HudState& out, erov::SteveState& steve);
void SetupOverlay();

} // namespace eldenring::loader_detail
