#include "core/loader_internal.hpp"
#include "eldenring_perfstats.hpp"

namespace eldenring::loader_detail {

static size_t g_tnt_prev_active_count = 0;

static int g_prev_mining_stage = -1;

static blocks::Cell g_prev_mining_cell{};

static blocks::TerrainMiningOverlay g_prev_terrain_mining{};

bool BlocksPresent() {
    std::lock_guard<std::mutex> g(g_blocks_mutex);
    return g_blocks.count() != 0;
}

void ApplyOriginRebase(float rx, float ry, float rz) {
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        g_blocks.shift(static_cast<int>(rx), static_cast<int>(ry), static_cast<int>(rz));
        g_tnt_mgr.shift(rx, ry, rz);
    }
    g_virtual_summons.shift(rx, ry, rz);
    SendBlockMesh();
}

void SendBlockMesh(const std::optional<blocks::MiningOverlay>& mining,
                   const std::optional<blocks::TerrainMiningOverlay>& terrain_mining) {
    perfstats::ScopedTimedLock g(g_blocks_mutex, perfstats::LockKind::Blocks);
    const size_t tnt_count = g_tnt_mgr.activeCount();
    const int stage = mining.has_value() ? mining->stage : -1;
    const blocks::Cell cell = mining.has_value() ? mining->cell : blocks::Cell{};
    const blocks::TerrainMiningOverlay terrain = terrain_mining.has_value() ? *terrain_mining : blocks::TerrainMiningOverlay{};
    if (g_blocks_sent_version == g_blocks.version() && tnt_count == 0 && g_tnt_prev_active_count == 0 &&
        stage == g_prev_mining_stage && cell == g_prev_mining_cell &&
        terrain.stage == g_prev_terrain_mining.stage &&
        std::memcmp(terrain.pos, g_prev_terrain_mining.pos, sizeof(terrain.pos)) == 0 &&
        std::memcmp(terrain.normal, g_prev_terrain_mining.normal, sizeof(terrain.normal)) == 0 &&
        terrain.lift == g_prev_terrain_mining.lift && terrain.half_size == g_prev_terrain_mining.half_size) return;
    g_blocks_sent_version = g_blocks.version();
    g_tnt_prev_active_count = tnt_count;
    g_prev_mining_stage = stage;
    g_prev_mining_cell = cell;
    g_prev_terrain_mining = terrain;
    erov::SetBlockMesh(blocks::buildBlockMesh(g_blocks, g_tnt_mgr.activePositions(), mining));
    erov::SetTerrainCrackMesh(terrain_mining.has_value() ? blocks::buildTerrainCrackMesh(*terrain_mining) : mc::rig::RigMesh{});
}

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

static float AimPitchDeg(const float dir[3]) {
    const float horizontal = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
    return std::atan2(-dir[1], horizontal) * 57.2957795f;
}

BlockTarget FindBlockTarget() {
    BlockTarget t;
    float eye[3], dir[3];
    if (!AimRay(eye, dir)) return t;
    t.aim_ok = true;
    for (int i = 0; i < 3; ++i) {
        t.eye[i] = eye[i];
        t.dir[i] = dir[i];
    }
    t.pitch_deg = AimPitchDeg(dir);
    const float reach = g_reach.load();
    std::optional<blocks::GridHit> grid_hit;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        grid_hit = blocks::raycastGrid(g_blocks, eye, dir, reach);
    }
    RayResult world_hit;
    if (g_los_enabled.load() || (g_ground_mining_enabled.load() && t.pitch_deg >= mc::GroundMiningController::kMinPitchDeg)) {
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
        t.terrain_hit = true;
        t.place = blocks::placementCellForWorldHit(world_hit.pos, world_hit.normal);
        for (int i = 0; i < 3; ++i) {
            t.terrain_pos[i] = world_hit.pos[i];
            t.terrain_normal[i] = world_hit.normal[i];
        }
    }
    return t;
}

bool AimingAtChest(int out_cell[3]) {
    float eye[3], dir[3];
    if (!AimRay(eye, dir)) return false;
    std::lock_guard<std::mutex> g(g_blocks_mutex);
    const std::optional<blocks::GridHit> hit = blocks::raycastGrid(g_blocks, eye, dir, g_reach.load());
    if (!hit.has_value() || g_blocks.get(hit->cell) != mc::BlockId::Chest) return false;
    out_cell[0] = hit->cell.x;
    out_cell[1] = hit->cell.y;
    out_cell[2] = hit->cell.z;
    return true;
}

bool AimingAtCraftingTable() {
    float eye[3], dir[3];
    if (!AimRay(eye, dir)) return false;
    std::lock_guard<std::mutex> g(g_blocks_mutex);
    const std::optional<blocks::GridHit> hit = blocks::raycastGrid(g_blocks, eye, dir, g_reach.load());
    return hit.has_value() && g_blocks.get(hit->cell) == mc::BlockId::CraftingTable;
}

const char* BlockSound(mc::BlockId id) {
    switch (id) {
        case mc::BlockId::Stone: return "block.stone.step";
        case mc::BlockId::StoneBricks:
        case mc::BlockId::Bricks:
        case mc::BlockId::StoneSlab:
        case mc::BlockId::Glass:
        case mc::BlockId::Anvil: return "block.stone.step";
        case mc::BlockId::Dirt: return "block.gravel.step";
        case mc::BlockId::Gravel: return "block.gravel.step";
        case mc::BlockId::Sand: return "block.sand.step";
        case mc::BlockId::Leaves: return "block.grass.step";
        case mc::BlockId::Wood:
        case mc::BlockId::WoodPlanks:
        case mc::BlockId::Torch:
        case mc::BlockId::Chest:
        case mc::BlockId::CraftingTable: return "block.wood.step";
        default: return "block.grass.step";
    }
}

namespace {
struct HandStack {
    mc::ItemId item{mc::ItemId::None};
    unsigned count{0};
    int slot{-1}; // the selected hotbar slot for the main hand, -1 for the offhand
};

HandStack StackInHand(mc::Hand hand) {
    std::lock_guard<std::mutex> g(g_melee_mutex);
    if (hand == mc::Hand::Main) {
        return {g_melee.heldItem(), g_melee.countAt(g_melee.selectedSlot()), g_melee.selectedSlot()};
    }
    return {g_melee.offhandItem(), g_melee.offhandCount(), -1};
}

// Caller holds g_melee_mutex.
bool ConsumeFromHand(mc::Hand hand, mc::ItemId item, int slot) {
    if (!creative::consumesItems(CurrentGameMode())) return true; // creative: the stack is not used up
    return hand == mc::Hand::Main ? g_melee.consumeAt(slot) : g_melee.consumeOffhand(item);
}
} // namespace

bool TryUseSpawnEgg(mc::Hand hand) {
    const HandStack stack = StackInHand(hand);
    if (stack.count == 0 || !mc::isSpawnEgg(stack.item)) return false;
    const std::string_view mob_type = mc::spawnEggToMobType(stack.item);
    const std::string mob_str = mob_type.empty() ? "zombie" : std::string(mob_type);
    if (!QueueSummon("spawn egg", mob_str.c_str())) return true; // refused (too soon): the click is still the egg's
    std::lock_guard<std::mutex> g(g_melee_mutex);
    ConsumeFromHand(hand, stack.item, stack.slot);
    g_melee.startSwing(hand);
    return true;
}

static bool CanIgniteTnt(mc::Hand hand) {
    if (!g_blocks_enabled.load()) return false;
    const BlockTarget t = FindBlockTarget();
    if (!t.have || !t.on_block) return false;
    mc::BlockId target_block = mc::BlockId::Air;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        target_block = g_blocks.get(t.cell);
    }
    if (target_block != mc::BlockId::Tnt) return false;
    const HandStack stack = StackInHand(hand);
    return eldenring::tnt::shouldIgniteTntOnRightClick(stack.item, target_block);
}

bool TryIgniteTnt(mc::Hand hand) {
    if (!g_blocks_enabled.load()) return false;
    const BlockTarget t = FindBlockTarget();
    if (!t.have || !t.on_block) return false;

    mc::BlockId target_block = mc::BlockId::Air;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        target_block = g_blocks.get(t.cell);
    }
    if (target_block != mc::BlockId::Tnt) return false;

    const HandStack stack = StackInHand(hand);
    // Only FlintAndSteel ignites TNT on right click (Minecraft rule)
    if (!eldenring::tnt::shouldIgniteTntOnRightClick(stack.item, target_block)) {
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
        g_melee.startSwing(hand);
    }
    return true;
}

static bool CanIgniteCreeper(mc::Hand hand) {
    const HandStack stack = StackInHand(hand);
    if (stack.item != mc::ItemId::FlintAndSteel) return false;
    std::vector<eldenring::mobs::MobSnapshot> summons;
    {
        std::lock_guard<std::mutex> g(g_summon_mutex);
        summons = g_summon_list;
    }
    float player_feet[3]{};
    if (!detail::readPhysicsPosition(g_reader, g_img.base, PlayerChrPtr(), player_feet)) return false;
    for (const auto& s : summons) {
        if (s.model_type != "creeper") continue;
        const float dx = s.feet[0] - player_feet[0];
        const float dy = s.feet[1] - player_feet[1];
        const float dz = s.feet[2] - player_feet[2];
        if (dx * dx + dy * dy + dz * dz <= 4.5f * 4.5f) return true;
    }
    return false;
}

bool CanIgnite(mc::Hand hand) { return CanIgniteTnt(hand) || CanIgniteCreeper(hand); }

bool TryIgniteCreeper(mc::Hand hand) {
    const HandStack stack = StackInHand(hand);
    if (stack.item != mc::ItemId::FlintAndSteel) return false;

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
                g_melee.startSwing(hand);
                return true;
            }
        }
    }
    return false;
}

bool CanPlaceBlock(mc::Hand hand) {
    if (!g_blocks_enabled.load()) return false;
    const HandStack stack = StackInHand(hand);
    if (stack.count == 0) return false;
    const mc::BlockId id = blocks::blockForItem(stack.item);
    if (id == mc::BlockId::Air) return false;
    const BlockTarget t = FindBlockTarget();
    if (!t.have) return false;
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (g_blocks.get(t.place) != mc::BlockId::Air) return false;
    }
    const uintptr_t player = PlayerChrPtr();
    float feet[3];
    if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet) && blocks::cellTouchesPlayer(t.place, id, feet)) return false;
    return true;
}

bool TryPlaceBlock(mc::Hand hand) {
    if (!g_blocks_enabled.load()) return false;
    const HandStack stack = StackInHand(hand);
    if (stack.count == 0) return false;
    const mc::BlockId id = blocks::blockForItem(stack.item);
    if (id == mc::BlockId::Air) return false;
    const BlockTarget t = FindBlockTarget();
    // No target (or the cell is blocked): the click is not consumed, so the offhand gets its turn.
    if (!t.have) return false;
    const uintptr_t player = PlayerChrPtr();
    float feet[3];
    if (player != 0 && detail::readPhysicsPosition(g_reader, g_img.base, player, feet) && blocks::cellTouchesPlayer(t.place, id, feet)) return false; // not inside yourself
    {
        std::lock_guard<std::mutex> g(g_blocks_mutex);
        if (!g_blocks.place(t.place, id)) return false;
        if (id == mc::BlockId::Chest) g_block_entities.create(t.place, mc::BlockEntityKind::Chest);
    }
    {
        std::lock_guard<std::mutex> g(g_melee_mutex);
        ConsumeFromHand(hand, stack.item, stack.slot);
        g_melee.startSwing(hand);
    }
    SendBlockMesh();
    eraudio::Play(BlockSound(id), 0.8f);
    Log("blocks: placed %d at (%d %d %d)", static_cast<int>(id), t.place.x, t.place.y, t.place.z);
    return true;
}

} // namespace eldenring::loader_detail
