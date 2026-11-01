#pragma once

// D3D12 overlay for Elden Ring (milestone 1): draws a Dear ImGui HUD into the swap chain's back buffer just before
// the game presents. Hooks (vtable function addresses taken from a throw-away device, MinHook):
//   ID3D12CommandQueue::ExecuteCommandLists - learns the DIRECT queue the game renders with (verified: a single one),
//   IDXGISwapChain::Present                 - records and submits our command list on that queue,
//   IDXGISwapChain::ResizeBuffers           - drops our resources so they are rebuilt after the resize.

#include "eldenring_mobs.hpp"
#include "eldenring_virtual_summon.hpp"
#include "mc/entity_model.hpp"
#include "mc/rig.hpp"
#include "ow_viewmodel.hpp"

#include <vector>

namespace erov {

struct HudState {
    bool show{false};     // false: draw nothing this frame (title, menus, loading, fades...)
    bool mc_mode{false};  // F6 toggle: show the Minecraft HUD
    bool cs_mode{false};  // F1 toggle: show CS2 viewmodel and crosshair
    bool ow_mode{false};  // Overwatch mode: draw the Doomfist gauntlet instead
    int hp{0};
    int max_hp{0};
    int selected_slot{0}; // 0..8
    float hit{0.f};       // crosshair hit marker 0..1 (fades by itself)
    bool hit_crit{false}; // that marker is for a critical hit
    float kill{0.f};      // kill marker 0..1
    uint16_t hotbar[9]{}; // mc::ItemId of each hotbar slot
    uint8_t hotbar_count[9]{}; // stack sizes (shown when above 1)
    float absorption_mc{0.f}; // golden hearts, in Minecraft hit points (2 per heart)
    float eating{0.f};    // 0..1 progress of the meal being eaten (0 = not eating)
    float totem{0.f};     // 0..1 totem-of-undying pop animation (1 = just popped)
    bool creative{false}; // F5: creative mode (item palette, no health/hunger/xp bars)
    float mode_toast{0.f}; // 1 just switched .. 0: the mode name is shown in the top left corner
    int slot_probe{-1};   // >= 0: the F9 part-slot probe is on, this is the only hidden part slot
    // The inventory screen (all 36 slots, hotbar first, the stack on the cursor and the virtual mouse in screen pixels).
    bool inv_open{false};
    bool craft_open{false}; // the crafting table screen (3x3) instead of the inventory
    int inv_page{0};        // creative palette page
    int inv_page_count{1};
    uint16_t inv_item[36]{};
    uint8_t inv_count[36]{};
    uint16_t craft_item[9]{}; // the 3x3 crafting grid, row-major
    uint8_t craft_count[9]{};
    uint16_t craft_result_item{0};
    uint8_t craft_result_count{0};
    uint16_t cursor_item{0};
    uint8_t cursor_count{0};
    uint16_t offhand_item{0};
    uint8_t offhand_count{0};
    uint16_t armor_item[4]{};
    uint8_t armor_count[4]{};
    float mouse_x{0.f}, mouse_y{0.f};
    // Survival status bars: food level (0..20), the Regeneration effect (the hearts' wave), experience level and bar progress 0..1.
    int food{20};
    bool food_shaking{false};
    bool regen{false};
    int xp_level{0};
    float xp_progress{0.f};
    // Armor points (0..20) continuously synchronized for HUD armor bar
    int armor_points{0};
    // Armor damage reduction toast notification for live verification
    float armor_toast_timer{0.f};
    int armor_toast_raw{0};
    int armor_toast_final{0};
    int armor_toast_pts{0};
    float armor_toast_ratio{0.f};
};

// The 3D figure for this frame: camera, standing point (feet, game metres) and heading (rotation about +Y).
struct SteveState {
    bool draw{false};
    mc::rig::Camera cam{};
    float fov_y{0.8378f};
    float feet[3]{};
    float yaw{0.f};
    float swing{0.f}; // arm swing progress 0..1 (0 = idle)
    bool dead{false}; // the player's HP is 0: the figure falls over (Minecraft death flip)
    float hurt{0.f};  // 1 just hurt .. 0: the figure flashes red
    bool first_person{false};   // draw the view model (bare arm or held item) instead of the figure
    uint16_t held_item{0};      // mc::ItemId the hotbar wants in the hand (0 = bare hand)
    uint16_t offhand_item{0};   // mc::ItemId in offhand (0 = none)
    bool blocking{false};       // shield raise / blocking pose
    float cooldown{1.f};        // attack strength 0..1
    float eating{0.f};          // 0..1 progress of the meal in the hand (0 = not eating)
    float bow_charge{0.f};      // 0..1 how far the bow is drawn (0 = not drawing): the third-person arms aim
    bool block_offhand{false};  // the raised shield is held in the left hand: only the left arm is up
    bool bow_offhand{false};    // the bow that is drawn is in the offhand: the left arm aims and the offhand item is the drawn bow
    float bow_ticks{0.f};       // ticks the bow has been drawn (0 = not drawing): the first-person bow pose animates with it
    bool sneaking{false};       // sneaking/crouching: torso forward lean and lower stance
    bool sprinting{false};      // sprinting: forward lean and enlarged stride dynamics
    float speed_mps{0.f};       // horizontal speed, for the walking bob
    bool on_ground{true};
    bool sleeping{false};      // player resting at grace: sleeping kinematics
    bool shadow{false};        // draw the blob shadows under the figure and the summons (mc_er_steve.txt: shadow=1)
    bool ground_valid{false};  // ground_y is the height of the ground under the feet this frame (otherwise the feet's own height is used)
    float ground_y{0.f};
    bool cam_valid{false}; // cam/fov_y are a real camera this frame (also set while the figure itself is not drawn)
    // Phase A debug animation state (mc_er_dbg_anim.txt + Insert/Delete/Home/End).
    bool gliding{false};
    bool riptide{false};
    float trident_charge{0.f};
    uint32_t firework_seq{0};
};

// Tuning read from mc_er_steve.txt (key=value lines).
struct SteveConfig {
    bool occlusion{true};
    int debug{0};
    float depthview_gain{0.f}; // > 0: draw the scene depth as a grey overlay with this gain (calibration) // 1: colour the figure by how the scene depth compares with it (calibration)
    float depth_const{0.0501f};
    float rel_bias{0.08f};
    float abs_bias{0.05f};
    float scene_height{0.f}; // 0: use the back buffer height for the projection aspect
    bool depth_probe{false}; // record how the game uses its depth textures for three frames (depth_probe_d3d12.hpp); mc_er_depthprobe.txt switches it on
};
void SetSteveConfig(const SteveConfig& cfg);

// Tuning read from mc_er_ow.txt (key=value lines): the gauntlet's own FOV,
// scale and translation. Independent from the CS2/Steve config.
using OwConfig = eldenring::ow::OwViewmodelConfig;
void SetOwConfig(const OwConfig& cfg);
// The Steve skin (RGBA8, 64x64) the loader decoded; call before the overlay is created. Without it the figure is flat grey-brown.
void SetSteveSkin(const uint8_t* rgba, unsigned width, unsigned height);
// The HUD atlas (RGBA8, 256x256, the locally extracted mc_hud_atlas.png); call before the overlay is created. Without it the HUD
// is drawn with plain rectangles.
void SetHudAtlas(const uint8_t* rgba, unsigned width, unsigned height);
// Writes `frames` lines of per-frame figure and camera positions to the log (diagnosing jitter).
void RequestFrameTrace(int frames);

// The mobs' skin (RGBA8, 64x64 / 64x32): call before the overlay is created. Without it the mobs wear the Steve skin.
void SetMobSkin(const uint8_t* rgba, unsigned width, unsigned height);
void SetMobSkin(std::string_view name, const uint8_t* rgba, unsigned width, unsigned height);
// The mobs' model (a Bedrock geometry file parsed by the loader): call before the overlay is created. Without it the mobs are drawn with the Steve rig.
void SetMobModel(const mc::model::EntityModel& model);
void SetMobModel(std::string_view name, const mc::model::EntityModel& model);
// Registers a model under a name without changing the default mob model (the Phase A props).
void RegisterModel(std::string_view name, const mc::model::EntityModel& model);
// The mobs drawn over the game's summons this frame (positions and headings in game metres/radians). Thread safe, replaces the last list.
void SetMobs(const std::vector<eldenring::mobs::MobSnapshot>& mobs);
// In-flight arrows drawn this frame in 3D world space. Thread safe, replaces the last list.
void SetArrows(const std::vector<eldenring::summon::ArrowDraw>& arrows);

// Combat particles (Minecraft's critical hit stars, damage hearts, sweep arc) at a world position (game metres); `count` is the
// number of hearts for Damage. Thread safe; the overlay projects and draws them.
enum class FxKind { Crit, Damage, Sweep, Spark, Dust, Firework, Riptide };
void SpawnFx(FxKind kind, const float world_pos[3], int count);
// Same, with a direction of travel: Firework streams its sparks opposite `dir` (zero means straight up).
void SpawnFx(FxKind kind, const float world_pos[3], const float dir[3], int count);
// A CS hitscan tracer: a world-space segment (game metres) that fades out over `life_s`. Thread safe.
void SpawnTracer(const float from[3], const float to[3], float life_s = 0.08f);
#if ENABLE_CS2_VIEWMODEL
// A bullet hole on static geometry (floor, wall, rock): the surface position and the outward normal, in game
// metres. Thread safe; the overlay keeps a bounded list and fades them out.
void SpawnDecal(const float pos[3], const float normal[3]);
#endif
// The back buffer size of the last frame (0 before the first one); the inventory screen needs it to lay itself out.
void ScreenSize(float& width, float& height);
// The mesh of the placed blocks (world space, game metres; see eldenring_blockmesh.hpp). Thread safe; the overlay uploads and draws it.
void SetBlockMesh(const mc::rig::RigMesh& mesh);
// The ground crack's own mesh, drawn with a much smaller occlusion bias than the placed blocks.
void SetTerrainCrackMesh(const mc::rig::RigMesh& mesh);
// Saves the next frame (the game plus our overlay) as a PNG at `utf8_path`. Thread safe; the file is written by the Present thread.
void RequestScreenshot(const char* utf8_path);
// Duration (seconds) of a clip in the active CS viewmodel, or 0 when unavailable.
float CsClipDuration(const char* name);
// Debug: adjust the CS viewmodel projection. axis 0=x, 1=y, 2=z, 3=fov, 4=near.
void AdjustCsView(int axis, float delta);
// OW viewmodel tuning: axis 0=x, 1=y, 2=z, 3=fov, 4=scale, 5=shoot motion.
// Writes the new values back to mc_er_ow.txt so a restart keeps them.
void AdjustOwView(int axis, float delta);
void PersistOwConfig(const OwConfig& cfg);
// Duration (seconds) of an authored Overwatch clip, or 0 when unavailable.
float OwClipDuration(const char* name);
// Debug: bind the next captured 1920x1080 R32G8X24 depth resource (the game creates several).
void CycleDepthCandidate();

// Called on the Present thread once per frame; fills `out`. Return false to skip the overlay entirely.
using HudProvider = bool (*)(HudState& out, SteveState& steve);
using LogFn = void (*)(const char* fmt, ...);

// Creates the hooks. Returns false (and logs why) when anything fails; nothing is left half-installed.
// MinHook must already be initialised by the caller.
bool Install(HudProvider provider, LogFn log);

} // namespace erov
