#include "core/loader_internal.hpp"

namespace eldenring::loader_detail {

struct HitSnapshot {
    float pos[3];
    float normal[3];
    uint32_t shape;
    uint32_t material_index;
};

static constexpr uintptr_t kPlayerBlockId = 0x6D0;

static eldenring::mining::MiningController g_mining;

static mc::GroundMiningController g_ground_mining;

static uint64_t g_loot_rng = 0;

static constexpr uintptr_t kPhysicsVelocity = 0x120;

static constexpr float kGroundMiningMoveSpeed = 0.5f;

static HitSnapshot SnapshotHit(const void* hit) {
    HitSnapshot s{};
    const auto* b = static_cast<const uint8_t*>(hit);
    std::memcpy(s.pos, b, sizeof(s.pos));
    std::memcpy(s.normal, b + 0x10, sizeof(s.normal));
    std::memcpy(&s.shape, b + 0x48, sizeof(s.shape));
    std::memcpy(&s.material_index, b + 0x50, sizeof(s.material_index));
    return s;
}

void __fastcall HitCollectorDetour(void* self, const void* hit) {
    const HitSnapshot s = SnapshotHit(hit);
    if (g_hit_collector_orig) g_hit_collector_orig(self, hit);

    const uintptr_t player = PlayerChrPtr();
    float feet[3];
    if (player == 0 || !detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) return;
    const float dx = s.pos[0] - feet[0], dy = s.pos[1] - feet[1], dz = s.pos[2] - feet[2];
    // Only ground hits near the player: the mining ray starts at the eye and reaches ~4.5 m.
    if (dx * dx + dy * dy + dz * dz > 36.0f || s.normal[1] < 0.2f) return;

    uint32_t material = 0;
    if (g_material_lookup) g_material_lookup(&s.shape, s.material_index, &material, true);
    g_last_ground_material.store(material, std::memory_order_relaxed);
    g_last_ground_shape.store(s.shape, std::memory_order_relaxed);

    if (!g_zone_probe_log.load(std::memory_order_relaxed)) return;
    const uint64_t now = GetTickCount64();
    if (now - g_zone_probe_last_log_ms.load(std::memory_order_relaxed) < 200) return;
    g_zone_probe_last_log_ms.store(now, std::memory_order_relaxed);

    uint32_t raw = 0;
    if (!SafeCopy(player + kPlayerBlockId, &raw, sizeof(raw))) raw = 0;
    const mc::WorldBlockId block = mc::decodeBlockId(raw);
    Log("ZONEPROBE: block=0x%08X area=%u gx=%u gz=%u lod=%u pos=(%.2f %.2f %.2f) n=(%.2f %.2f %.2f) shape=0x%X matidx=%u material=%u",
        raw, block.area, block.grid_x, block.grid_z, block.lod, s.pos[0], s.pos[1], s.pos[2],
        s.normal[0], s.normal[1], s.normal[2], s.shape, s.material_index, material);
}

uint32_t PlayerBlockId() {
    const uintptr_t player = PlayerChrPtr();
    uint32_t raw = 0;
    return player != 0 && SafeCopy(player + kPlayerBlockId, &raw, sizeof(raw)) ? raw : 0;
}

static bool PlayerIsMoving() {
    const uintptr_t player = PlayerChrPtr();
    if (player == 0) return false;
    float v[3];
    if (!detail::readPhysicsVec3(g_reader, g_img.base, player, kPhysicsVelocity, v)) return false;
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) > kGroundMiningMoveSpeed;
}

static bool GroundBelow(float out_pos[3], float out_normal[3]) {
    const uintptr_t player = PlayerChrPtr();
    float feet[3];
    if (player == 0 || !detail::readPhysicsPosition(g_reader, g_img.base, player, feet)) return false;
    const float from[3] = {feet[0], feet[1] + 0.5f, feet[2]};
    const float disp[3] = {0.f, -(g_reach.load() + 1.0f), 0.f};
    const RayResult hit = CastStaticRay(from, disp);
    if (!hit.ok || !hit.hit || feet[1] - hit.pos[1] > 2.0f) return false;
    for (int i = 0; i < 3; ++i) {
        out_pos[i] = hit.pos[i];
        out_normal[i] = hit.normal[i];
    }
    return true;
}

static const char* GroundHitSound(mc::LootZone zone) {
    return zone == mc::LootZone::Grassland ? "block.grass.step" : "block.stone.step";
}

bool StepMining(float dt, bool left_held) {
    if (!g_blocks_enabled.load()) {
        g_mining.reset();
        SendBlockMesh(std::nullopt);
        return false;
    }
    // Never cast a world ray or touch the overlay while the game is loading or a menu is open: the
    // Havok world is in transition there, and the load screen is where the crash happened.
    if (!WantSuppress()) {
        g_mining.reset();
        g_ground_mining.reset();
        SendBlockMesh(std::nullopt, std::nullopt);
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

    // Ground mining (issues #66/#81): aim at the terrain under the player's feet with an empty hand
    // or a pickaxe. A placed block keeps the progressive block-mining path below.
    const bool ground_target = t.have && t.terrain_hit && !t.on_block &&
                               mc::GroundMiningController::canDigSurface(t.terrain_normal[1]);
    const float pitch = t.pitch_deg;
    if (g_ground_mining_enabled.load() && ground_target && mc::GroundMiningController::canDig(held, pitch)) {
        // The aim ray decides the target and the loot. The crack uses the same hit; the configured
        // lift compensates the measured collision-surface / visible-floor gap (about 14 cm here).
        const int configured_zone = g_loot_zone.load(std::memory_order_relaxed);
        const uint32_t block_raw = configured_zone < 0 ? PlayerBlockId() : 0;
        const uint32_t ground_material = g_last_ground_material.load(std::memory_order_relaxed);
        const mc::LootZone zone = configured_zone < 0
            ? mc::classifyZone(mc::decodeBlockId(block_raw), ground_material, g_zone_rules)
            : static_cast<mc::LootZone>(std::clamp(configured_zone, 0, static_cast<int>(mc::LootZone::Count) - 1));
        if (g_loot_rng == 0) g_loot_rng = GetTickCount64() | 1;
        g_ground_mining.update(dt, left_held, pitch, held, zone, is_creative, g_loot_rng, PlayerIsMoving());
        const mc::GroundMiningState& gs = g_ground_mining.state();
        const float* fx_pos = t.terrain_pos;
        const float* fx_normal = t.terrain_normal;
        float below_pos[3], below_normal[3];
        const bool below = GroundBelow(below_pos, below_normal);
        if (below) {
            fx_pos = below_pos;
            fx_normal = below_normal;
        }
        if (gs.is_mining) {
            blocks::TerrainMiningOverlay terrain{};
            std::memcpy(terrain.pos, fx_pos, sizeof(terrain.pos));
            std::memcpy(terrain.normal, fx_normal, sizeof(terrain.normal));
            terrain.stage = gs.stage;
            terrain.lift = g_ground_crack_lift.load(std::memory_order_relaxed);
            terrain.half_size = 0.5f * g_ground_crack_size.load(std::memory_order_relaxed);
            if (gs.hit_sound) {
                {
                    std::lock_guard<std::mutex> g(g_melee_mutex);
                    g_melee.startSwing();
                    eraudio::Play(GroundHitSound(zone), 0.6f);
                }
                static uint64_t last_anchor_log_ms = 0;
                const uint64_t now = GetTickCount64();
                if (g_zone_probe_log.load(std::memory_order_relaxed) && now - last_anchor_log_ms > 1000) {
                    last_anchor_log_ms = now;
                    float feet[3];
                    if (detail::readPhysicsPosition(g_reader, g_img.base, PlayerChrPtr(), feet)) {
                        Log("GROUNDANCHOR: feet=(%.2f %.2f %.2f) aim=(%.2f %.2f %.2f) ground=(%.2f %.2f %.2f) below=%d n=(%.2f %.2f %.2f) lift=%.3f mat=%u",
                            feet[0], feet[1], feet[2], t.terrain_pos[0], t.terrain_pos[1], t.terrain_pos[2],
                            fx_pos[0], fx_pos[1], fx_pos[2], below ? 1 : 0,
                            t.terrain_normal[0], t.terrain_normal[1], t.terrain_normal[2], terrain.lift, ground_material);
                    }
                }
            }
            SendBlockMesh(std::nullopt, terrain);
            return true;
        }
        if (gs.broken) {
            {
                std::lock_guard<std::mutex> g(g_melee_mutex);
                g_melee.inventory().add({gs.drop.item, 1});
                if (gs.drop.bonus != mc::ItemId::None) g_melee.inventory().add({gs.drop.bonus, 1});
                g_melee.startSwing();
            }
            eraudio::Play("entity.experience_orb.pickup", 0.9f);
            SendBlockMesh(std::nullopt, std::nullopt);
            Log("ground: broke terrain at (%.2f %.2f %.2f) anchor=aim zone=%d item=%d blocked=%d pity=%d combo=%d bonus=%d block=0x%08X mat=%u auto=%d",
                fx_pos[0], fx_pos[1], fx_pos[2], static_cast<int>(zone),
                static_cast<int>(gs.drop.item), gs.drop.tier_blocked ? 1 : 0, gs.drop.pity_triggered ? 1 : 0,
                gs.drop.combo_triggered ? 1 : 0, static_cast<int>(gs.drop.bonus), block_raw, ground_material,
                configured_zone < 0 ? 1 : 0);
            return true;
        }
    } else {
        g_ground_mining.reset();
    }

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
        const mc::ItemId drop = eldenring::mining::MiningController::blockDrop(id, held, is_creative);
        if (drop != mc::ItemId::None) {
            std::lock_guard<std::mutex> g(g_melee_mutex);
            g_melee.inventory().add({drop, 1});
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

} // namespace eldenring::loader_detail
