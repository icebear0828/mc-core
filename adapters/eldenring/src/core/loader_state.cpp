// Shared state for the Elden Ring loader modules (issue #96).
//
// The pre-split loader.cpp kept every global in one anonymous namespace. They
// now live here with external linkage so the domain modules can share them;
// this is a pure code motion, the values and defaults are unchanged.
#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

const std::string& g_game_dir = eldenring::diagnostics::GetGameDir();
const SelfReader g_reader;

ImageInfo g_img;
uint32_t g_rva_world = 0, g_rva_menu = 0, g_rva_loading = 0, g_rva_fade = 0;
std::atomic<DWORD> g_game_tid{0};

RuntimeContext& g_ctx = RuntimeContext::Instance();
ModeConfig& g_mode = g_ctx.mode();
PlayerSystems& g_player = g_ctx.player();
WorldSystems& g_world = g_ctx.world();
CombatSystems& g_combat = g_ctx.combat();
UiSystems& g_ui = g_ctx.ui();

creative::GameMode CurrentGameMode() { return g_ctx.currentGameMode(); }

std::atomic<bool>& g_damage_enabled = g_mode.damage_enabled;
std::atomic<bool>& g_mc_mode = g_mode.mc_mode;
std::atomic<uint8_t>& g_game_mode = g_mode.game_mode;
std::atomic<uint64_t>& g_mode_toast_ms = g_mode.mode_toast_ms;
std::atomic<bool>& g_input_enabled = g_mode.input_enabled;
std::atomic<bool>& g_click_attack = g_mode.click_attack;
std::atomic<bool>& g_steve_enabled = g_mode.steve_enabled;
std::atomic<bool>& g_hide_native = g_mode.hide_native;
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
static NoCombatPort g_no_combat_port;
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
ApplyHpFn g_apply_hp = nullptr;
HitFeedback& g_feedback = g_combat.feedback;
thread_local int32_t t_hp_after = -1; // victim hp right after our vfunc[7] call (game thread)
std::optional<HitTemplate>& g_template = g_combat.hit_template;

std::atomic<bool> g_no_stagger{false};
std::atomic<unsigned> g_stagger_skipped{0};
std::atomic<bool> g_fall_protect{true};
std::atomic<uint64_t> g_movement_layer_ms{0}; // last time one of our movement features moved the player (tick count)
std::atomic<unsigned> g_kills_skipped{0};
std::mutex g_cr_mutex;
creative::FallLogState g_cr_fall_state;
std::atomic<bool> g_cr_log{false}; // mc_er_creativelog.txt: log-only probes
std::atomic<bool> g_summon_hide{false};

std::atomic<bool>& g_inv_open = g_ui.inv_open;
std::atomic<float>& g_inv_mx = g_ui.inv_mx;
std::atomic<float>& g_inv_my = g_ui.inv_my;
std::atomic<int>& g_inv_vk = g_ui.inv_vk;
std::atomic<float>& g_inv_sens = g_ui.inv_sens;
std::atomic<unsigned>& g_isblocked_forced = g_ui.isblocked_forced;
std::atomic<unsigned>& g_menufreeze_forced = g_ui.menufreeze_forced;

CamKeep g_cam_keep;
std::atomic<bool> g_camstep_installed{false}; // the camera update hook (game thread, every frame) is in place
std::atomic<unsigned> g_camstep_calls{0}, g_camstep_changed{0};

std::mutex g_summon_mutex;
std::vector<eldenring::mobs::MobSnapshot> g_summon_list; // the team 47 summons seen by the last HideSummonModels (render thread), read by the HUD provider
std::vector<eldenring::summon::ArrowDraw> g_summon_arrows; // active in-flight arrows from virtual summons
float g_mob_yaw_offset = eldenring::live::summon::kDefaultMobYawOffset; // monster/wolf orientation quaternion aligns forward; default 0 (mob_yaw_offset_deg)
eldenring::summon::VirtualSummonManager g_virtual_summons;
SummonWatch g_summon_watch;
std::atomic<uint64_t> g_last_summon_ms{0};
std::set<uintptr_t> g_hidden_flag_addresses; // the disp_flags words whose drawn bit we cleared (render thread only), to put it back
std::map<uintptr_t, uint16_t> g_suppressed_sfx_slots; // the sfx slot control words whose skip bit we set, to restore on exit

std::atomic<unsigned> g_hitvfx_skipped{0};
std::atomic<bool> g_los_enabled{false};
std::atomic<unsigned> g_los_blocked{0};

std::atomic<bool> g_ground_mining_enabled{true};
std::atomic<int> g_loot_zone{0}; // mc_er_steve.txt: loot_zone=0..5, or -1 = auto (block id + material)
std::atomic<float> g_ground_crack_lift{0.01f}; // mc_er_steve.txt: ground_crack_lift=<m> (negative sinks it)
std::atomic<float> g_ground_crack_size{0.9f};  // mc_er_steve.txt: ground_crack_size=<m> (full edge length)

static constexpr mc::ZoneRule kDefaultZoneAreaRules[] = {
    {60, mc::LootZone::Grassland}, // open-world overworld
    {61, mc::LootZone::Cave},      // underground map
};
std::vector<mc::ZoneRule> g_zone_material_rules;
std::vector<mc::ZoneRule> g_zone_area_rules;
mc::ZoneRules g_zone_rules{{}, mc::er_region::kTable, kDefaultZoneAreaRules, mc::LootZone::StoneSurface};

std::span<const mc::ZoneRule> DefaultZoneAreaRules() { return kDefaultZoneAreaRules; }

std::atomic<bool> g_zone_probe_log{false};
std::atomic<uint32_t> g_last_ground_material{0};
std::atomic<uint32_t> g_last_ground_shape{0};
std::atomic<uint64_t> g_zone_probe_last_log_ms{0};
HitCollectorFn g_hit_collector_orig = nullptr;
MaterialLookupFn g_material_lookup = nullptr;

std::atomic<bool> g_pdc_hooked{false};
std::atomic<bool> g_log_player_hits{false}; // mc_er_hitlog.txt: log the HitContext of every hit the player takes (read only)

std::atomic<bool>& g_shield_blocking = g_mode.shield_blocking;
std::atomic<bool>& g_shield_offhand = g_mode.shield_offhand; // the shield being raised is the offhand's (the left arm goes up), else the main hand's

std::atomic<bool> g_proj_log{false};
std::atomic<bool> g_arrow_damage{false};
std::atomic<int> g_proj_log_left{80};
std::atomic<bool>& g_bow_offhand = g_mode.bow_offhand;
std::atomic<float>& g_bow_power = g_mode.bow_power;
std::atomic<float>& g_bow_ticks = g_mode.bow_ticks;

std::atomic<uint64_t> g_guard_sound_ms{0};
std::atomic<bool> g_knockdown_fix{true};

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

std::atomic<bool> g_fall_reset{true};
std::atomic<float> g_fall_hold{1.0f};
std::atomic<float> g_pre_fall_t{0.f};
std::atomic<float> g_sprint_boost{2.5f}; // mc_er_steve.txt: sprint_boost=<m/s> (extra speed boost applied while sprinting on ground, default 2.5 m/s)
std::atomic<bool> g_mc_jump{false};
std::atomic<int> g_mc_jump_vk{VK_SPACE};
std::atomic<float> g_mc_jump_speed{eldenring::jump::kMcJumpInitialVelocity};
std::atomic<float> g_mc_gravity{eldenring::jump::kMcJumpGravity};
std::atomic<bool> g_flying{false};
std::atomic<float> g_fly_h_speed{flight::kHorizontalSpeed};
std::atomic<float> g_fly_v_speed{flight::kVerticalSpeed};
std::atomic<float> g_fly_yaw{0.f};
std::atomic<bool> g_fly_sync{true};

std::atomic<unsigned> g_bullet_seen{0};
std::atomic<uint64_t> g_last_bullet_fire_ms{0};

DirectInput8Create_t g_system_create = nullptr;
std::atomic<bool> g_wants_input_hooks{false};

} // namespace eldenring::loader_detail
