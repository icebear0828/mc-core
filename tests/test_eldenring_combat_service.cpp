#include <gtest/gtest.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "eldenring_combat_service.hpp"
#include "eldenring_runtime_context.hpp"
#include "mc/inventory.hpp"
#include "mc/consumables.hpp"

using namespace eldenring::combat;
using namespace eldenring::live;
using eldenring::RuntimeContext;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr uintptr_t kWorld = 0x7ff500000000ull;
constexpr uintptr_t kPlayer = 0x7ff500100000ull;
constexpr uintptr_t kEnemy1 = 0x7ff501000000ull;
constexpr uintptr_t kEnemy2 = 0x7ff502000000ull;

class TestCombatMem : public IMemoryReader {
public:
    std::map<uintptr_t, std::vector<uint8_t>> regions;
    bool read(uintptr_t address, void* out, size_t size) const override {
        for (const auto& [start, bytes] : regions) {
            if (address >= start && address + size <= start + bytes.size()) {
                std::memcpy(out, bytes.data() + (address - start), size);
                return true;
            }
        }
        return false;
    }
    void region(uintptr_t start, size_t size) { regions[start].assign(size, 0); }
    template <typename T>
    void put(uintptr_t address, const T& v) {
        for (auto& [start, bytes] : regions) {
            if (address >= start && address + sizeof(T) <= start + bytes.size()) {
                std::memcpy(bytes.data() + (address - start), &v, sizeof(T));
                return;
            }
        }
    }
    void writeBytes(uintptr_t addr, const void* src, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(src);
        for (size_t i = 0; i < size; ++i) {
            put<uint8_t>(addr + i, p[i]);
        }
    }
};

void spawnTestChr(TestCombatMem& m, uintptr_t chr, uint32_t vtable_rva, uint8_t team, int32_t hp, int32_t max_hp, uint32_t damage_vtable_rva) {
    const uintptr_t container = chr + 0x1000, data = chr + 0x2000, phys = chr + 0x3000, dmg = chr + 0x3800;
    m.region(chr, 0x4000);
    m.put<uint64_t>(chr, kBase + vtable_rva);
    m.put<uint8_t>(chr + layout::kTeamTypeInChrIns, team);
    m.put<uint64_t>(chr + layout::kModuleContainerInChrIns, container);
    m.put<uint64_t>(container + layout::kChrDataModuleSlot * 8, data);
    m.put<uint64_t>(container + layout::kPhysicsModuleSlot * 8, phys);
    m.put<uint64_t>(container + layout::kDamageModuleSlot * 8, dmg);
    m.put<uint64_t>(data, kBase + layout::kChrDataModuleVtableRva);
    m.put<uint64_t>(data + layout::kOwnerInDataModule, chr);
    m.put<int32_t>(data + layout::kDataHp, hp);
    m.put<int32_t>(data + layout::kDataMaxHp, max_hp);
    m.put<uint64_t>(phys, kBase + layout::kPhysicsModuleVtableRva);
    m.put<uint64_t>(phys + layout::kOwnerInDataModule, chr);
    const float p[3] = {0.f, 0.f, 0.f};
    for (int i = 0; i < 3; ++i) m.put<float>(phys + layout::kPhysicsPosition + 4 * i, p[i]);
    m.put<uint64_t>(dmg, kBase + damage_vtable_rva);
    m.put<uint64_t>(dmg + layout::kOwnerInDataModule, chr);
}

TestCombatMem makeTestWorld() {
    TestCombatMem m;
    m.region(kBase + layout::kWorldChrManGlobalRva, 8);
    m.region(kWorld, 0x20000);
    m.put<uint64_t>(kBase + layout::kWorldChrManGlobalRva, kWorld);
    m.put<uint64_t>(kWorld + layout::kPlayerInsInWorldChrMan, kPlayer);
    spawnTestChr(m, kPlayer, layout::kPlayerInsVtableRva, 1, 500, 500, 0x2A3A3E0);
    spawnTestChr(m, kEnemy1, layout::kEnemyInsVtableRva, 6, 219, 219, layout::kEnemyDamageModuleVtableRva);
    spawnTestChr(m, kEnemy2, layout::kEnemyInsVtableRva, 6, 100, 100, layout::kEnemyDamageModuleVtableRva);
    m.put<float>(kEnemy1 + 0x3000 + layout::kPhysicsPosition, 20.f);
    m.put<float>(kEnemy2 + 0x3000 + layout::kPhysicsPosition, 10.f);

    const uint32_t kinds[4] = {layout::kRelHostileRva, layout::kRelFriendRva, layout::kRelNeutralRva, layout::kRelAllRva};
    const uint32_t vts[4] = {layout::kRelHostileVtableRva, layout::kRelFriendVtableRva, layout::kRelNeutralVtableRva, layout::kRelAllVtableRva};
    m.region(kBase + layout::kTeamMatrixRva, layout::kTeamCount * layout::kTeamCount * 8);
    for (int i = 0; i < 4; ++i) {
        m.region(kBase + kinds[i], 8);
        m.put<uint64_t>(kBase + kinds[i], kBase + vts[i]);
    }
    for (uint32_t a = 0; a < layout::kTeamCount; ++a) {
        for (uint32_t b = 0; b < layout::kTeamCount; ++b) {
            m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (a * layout::kTeamCount + b) * 8, kBase + layout::kRelNeutralRva);
        }
    }
    m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (1 * layout::kTeamCount + 6) * 8, kBase + layout::kRelHostileRva);
    return m;
}

HitTemplate makeTestTemplate(uint8_t fill = 0xAB) {
    std::vector<uint8_t> bytes(layout::kHitTemplateSize, fill);
    return *HitTemplate::fromBytes(bytes.data(), bytes.size());
}

} // namespace

TEST(EldenRingCombatService, ShieldEvaluationBlocksInFrontArcAndAllowsFlank) {
    auto& svc = CombatService::Instance();
    svc.resetForTesting();

    const uintptr_t kP = 0x1000;
    const uintptr_t kFrontAttacker = 0x2001;
    const uintptr_t kRearAttacker = 0x2002;
    const uintptr_t kFlankAttacker = 0x2003;
    const uintptr_t kDiagonalAttacker = 0x2004;

    float p_feet[3] = {0.f, 0.f, 0.f};
    float front_feet[3] = {0.f, 0.f, 5.f};      // 0 deg (directly ahead)
    float rear_feet[3] = {0.f, 0.f, -5.f};      // 180 deg (directly behind)
    float flank_feet[3] = {5.f, 0.f, 0.f};      // 90 deg (flank)
    float diag_feet[3] = {1.f, 0.f, 3.f};       // ~18.4 deg (well within 100 deg arc)

    CombatCallbacks cb{};
    cb.get_player_chr = [&]() { return kP; };
    cb.read_physics_pos = [&](uintptr_t chr, float out_pos[3]) {
        if (chr == kP) std::memcpy(out_pos, p_feet, sizeof(p_feet));
        else if (chr == kFrontAttacker) std::memcpy(out_pos, front_feet, sizeof(front_feet));
        else if (chr == kRearAttacker) std::memcpy(out_pos, rear_feet, sizeof(rear_feet));
        else if (chr == kFlankAttacker) std::memcpy(out_pos, flank_feet, sizeof(flank_feet));
        else if (chr == kDiagonalAttacker) std::memcpy(out_pos, diag_feet, sizeof(diag_feet));
        else return false;
        return true;
    };
    cb.read_camera = [](CameraPose& cam) {
        // Camera looking directly down +Z
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.position[0] = 0.f; cam.position[1] = 1.6f; cam.position[2] = -1.f;
        return true;
    };
    cb.read_physics_orient = [](uintptr_t, float q[4]) {
        q[0] = 0.f; q[1] = 0.f; q[2] = 0.f; q[3] = 1.f;
        return true;
    };
    svc.setCallbacks(cb);

    // Front attack -> BLOCKED
    ShieldVerdict v_front = svc.evaluateShield(kP, kFrontAttacker);
    EXPECT_TRUE(v_front.blocks);
    EXPECT_GT(v_front.dot, eldenring::shield::kBlockMinDot);

    // Rear attack -> NOT BLOCKED
    ShieldVerdict v_rear = svc.evaluateShield(kP, kRearAttacker);
    EXPECT_FALSE(v_rear.blocks);
    EXPECT_LT(v_rear.dot, 0.f);

    // Flank attack (90 deg) -> NOT BLOCKED (outside 100 deg cone)
    ShieldVerdict v_flank = svc.evaluateShield(kP, kFlankAttacker);
    EXPECT_FALSE(v_flank.blocks);
    EXPECT_LT(v_flank.dot, eldenring::shield::kBlockMinDot);

    // Diagonal attack (18.4 deg) -> BLOCKED
    ShieldVerdict v_diag = svc.evaluateShield(kP, kDiagonalAttacker);
    EXPECT_TRUE(v_diag.blocks);
    EXPECT_GT(v_diag.dot, eldenring::shield::kBlockMinDot);
}

TEST(EldenRingCombatService, PlayerHitSurvivalAbsorbsDamageAndPopsTotemAtLethalDamage) {
    auto& svc = CombatService::Instance();
    svc.resetForTesting();

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);

    TestCombatMem mem;
    const uintptr_t kP = 0x1000;
    const uintptr_t kModule = 0x5000;
    const uintptr_t kHitCtx = 0x7000;

    mem.region(kModule, 0x100);
    mem.region(kHitCtx, 0x300);

    // Setup owner in data module
    mem.put<uint64_t>(kModule + layout::kOwnerInDataModule, kP);

    // Setup incoming damage = 50 (lethal when HP is 20)
    int32_t incoming_dmg = 50;
    mem.put<int32_t>(kHitCtx + layout::kHitDamage, incoming_dmg);

    CombatCallbacks cb{};
    cb.get_player_chr = [&]() { return kP; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.read_vitals = [&](uintptr_t chr, Vitals& v) {
        if (chr == kP) {
            v.hp = 20;
            v.max_hp = 20;
            return true;
        }
        return false;
    };
    std::string last_sound;
    cb.play_sound = [&](const char* snd, float) { last_sound = snd; };
    svc.setCallbacks(cb);

    mc::MeleeController melee;
    melee.inventory().setOffhand({mc::ItemId::TotemOfUndying, 1});

    SurvivalState survival;
    HitFeedback feedback;
    HungerModel hunger;

    uint32_t popped_heal_target = 0;
    bool popped = svc.playerHitSurvival(reinterpret_cast<void*>(kModule), nullptr,
                                        reinterpret_cast<uint8_t*>(kHitCtx), 0,
                                        popped_heal_target, melee, survival, feedback, hunger, ctx);

    EXPECT_TRUE(popped);
    EXPECT_EQ(melee.inventory().offhand().item, mc::ItemId::None);
    EXPECT_EQ(last_sound, "item.totem.use");

    int32_t final_dmg = 0;
    mem.read(kHitCtx + layout::kHitDamage, &final_dmg, sizeof(final_dmg));
    EXPECT_LT(final_dmg, 20);
}

TEST(EldenRingCombatService, DamageQueueDrainEnforcesLineOfSightAndDamageApplication) {
    auto& svc = CombatService::Instance();
    svc.resetForTesting();

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().los_enabled.store(true);

    TestCombatMem world = makeTestWorld();
    HitTemplate tmpl = makeTestTemplate();

    DamageQueue queue;
    queue.enqueue(kEnemy1, 50, 100);
    queue.enqueue(kEnemy2, 75, 100);

    std::vector<uintptr_t> damaged_chrs;
    CombatCallbacks cb{};
    cb.reader = &world;
    cb.image_base = kBase;
    cb.has_line_of_sight = [&](const float[3], const float to[3]) {
        // If victim is at x = 10, LOS clear, if x = 20, LOS blocked
        return to[0] < 15.f;
    };
    cb.read_physics_pos = [&](uintptr_t chr, float out_pos[3]) {
        if (chr == kEnemy1) { out_pos[0] = 20.f; out_pos[1] = 0.f; out_pos[2] = 0.f; return true; }
        if (chr == kEnemy2) { out_pos[0] = 10.f; out_pos[1] = 0.f; out_pos[2] = 0.f; return true; }
        return false;
    };
    cb.read_vitals = [&](uintptr_t, Vitals& v) {
        v.hp = 100;
        v.max_hp = 100;
        return true;
    };
    cb.call_vfunc7 = [&](uintptr_t dmg_module, uintptr_t, void*) {
        damaged_chrs.push_back(dmg_module);
        return true;
    };

    svc.setCallbacks(cb);

    mc::Inventory inv;
    HitFeedback feedback;
    XpModel xp;

    // Drain queue
    svc.drainDamageQueue(queue, &tmpl, 0, 100, inv, feedback, xp, ctx);

    // Enemy 1 should NOT be damaged (LOS blocked)
    // Enemy 2 should BE damaged (LOS clear)
    ASSERT_EQ(damaged_chrs.size(), 1u);
    EXPECT_EQ(damaged_chrs[0], kEnemy2 + 0x3800);
}

TEST(EldenRingCombatService, BreakTestSoftensReactionForPlayerInMcMode) {
    auto& svc = CombatService::Instance();
    svc.resetForTesting();

    TestCombatMem mem;
    const uintptr_t kP = 0x1000;
    const uintptr_t kPlayerDmgMod = 0x4000;
    const uintptr_t kEnemyDmgMod = 0x4500;
    const uintptr_t kHitBuffer = 0x6000;

    mem.region(kPlayerDmgMod, 0x100);
    mem.region(kEnemyDmgMod, 0x100);
    mem.region(kHitBuffer, 0x100);

    mem.put<uint64_t>(kPlayerDmgMod + 8, kP);
    mem.put<uint64_t>(kPlayerDmgMod + layout::kDamageHitBuffer, kHitBuffer);

    uintptr_t kEnemyChr = 0x2000;
    mem.put<uint64_t>(kEnemyDmgMod + 8, kEnemyChr);

    CombatCallbacks cb{};
    cb.get_player_chr = [&]() { return kP; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    svc.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().knockdown_fix.store(true);

    uint8_t hit_ctx[0x300] = {};

    // 1. Player in MC mode: engine broke = true -> softened to false, force cleared
    bool broke = svc.handleBreakTest(reinterpret_cast<void*>(kPlayerDmgMod), hit_ctx, true, ctx);
    EXPECT_FALSE(broke);

    float force = -1.f;
    mem.read(kHitBuffer + layout::kHitBufferForce, &force, sizeof(force));
    EXPECT_FLOAT_EQ(force, 0.f);

    // 2. Enemy: engine broke = true -> preserved as true
    bool enemy_broke = svc.handleBreakTest(reinterpret_cast<void*>(kEnemyDmgMod), hit_ctx, true, ctx);
    EXPECT_TRUE(enemy_broke);
}
