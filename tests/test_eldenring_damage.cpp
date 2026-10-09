#include <gtest/gtest.h>

#include "eldenring_damage.hpp"

#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace eldenring::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr uintptr_t kWorld = 0x7ff500000000ull;
constexpr uintptr_t kPlayer = 0x7ff500100000ull;
constexpr uintptr_t kEnemy = 0x7ff501000000ull;
constexpr uintptr_t kEnemy2 = 0x7ff502000000ull;

class Mem : public IMemoryReader {
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
        FAIL() << "address not in any region";
    }
};

void spawn(Mem& m, uintptr_t chr, uint32_t vtable_rva, uint8_t team, int32_t hp, int32_t max_hp, uint32_t damage_vtable_rva) {
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
    const float p[3] = {13.f, 2.f, 24.f};
    for (int i = 0; i < 3; ++i) m.put<float>(phys + layout::kPhysicsPosition + 4 * i, p[i]);
    m.put<uint64_t>(dmg, kBase + damage_vtable_rva);
    m.put<uint64_t>(dmg + layout::kOwnerInDataModule, chr);
}

Mem makeWorld() {
    Mem m;
    m.region(kBase + layout::kWorldChrManGlobalRva, 8);
    m.region(kWorld, 0x20000);
    m.put<uint64_t>(kBase + layout::kWorldChrManGlobalRva, kWorld);
    m.put<uint64_t>(kWorld + layout::kPlayerInsInWorldChrMan, kPlayer);
    spawn(m, kPlayer, layout::kPlayerInsVtableRva, 1, 500, 500, 0x2A3A3E0);
    spawn(m, kEnemy, layout::kEnemyInsVtableRva, 6, 219, 219, layout::kEnemyDamageModuleVtableRva);
    spawn(m, kEnemy2, layout::kEnemyInsVtableRva, 6, 100, 100, layout::kEnemyDamageModuleVtableRva);
    // Relation matrix: player team 1 -> team 6 hostile, -> team 30 neutral.
    const uint32_t kinds[4] = {layout::kRelHostileRva, layout::kRelFriendRva, layout::kRelNeutralRva, layout::kRelAllRva};
    const uint32_t vts[4] = {layout::kRelHostileVtableRva, layout::kRelFriendVtableRva, layout::kRelNeutralVtableRva,
                             layout::kRelAllVtableRva};
    m.region(kBase + layout::kTeamMatrixRva, layout::kTeamCount * layout::kTeamCount * 8);
    for (int i = 0; i < 4; ++i) {
        m.region(kBase + kinds[i], 8);
        m.put<uint64_t>(kBase + kinds[i], kBase + vts[i]);
    }
    for (uint32_t a = 0; a < layout::kTeamCount; ++a)
        for (uint32_t b = 0; b < layout::kTeamCount; ++b)
            m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (a * layout::kTeamCount + b) * 8, kBase + layout::kRelNeutralRva);
    m.put<uint64_t>(kBase + layout::kTeamMatrixRva + (1 * layout::kTeamCount + 6) * 8, kBase + layout::kRelHostileRva);
    return m;
}

HitTemplate makeTemplate(uint8_t fill = 0xAB) {
    std::vector<uint8_t> bytes(layout::kHitTemplateSize, fill);
    return *HitTemplate::fromBytes(bytes.data(), bytes.size());
}

struct Call {
    uintptr_t module;
    uintptr_t attacker;
    std::vector<uint8_t> ctx;
};

DrainContext baseCtx(const Mem& m, const HitTemplate& t, std::vector<Call>& calls) {
    DrainContext c;
    c.reader = &m;
    c.image_base = kBase;
    c.on_game_thread = true;
    c.tick = 10;
    c.hit_template = &t;
    c.updating_data_module = 0;
    c.require_victim_updating = false;
    c.invoke = [&calls](uintptr_t module, uintptr_t attacker, void* ctx) {
        Call call{module, attacker, {}};
        call.ctx.assign(static_cast<uint8_t*>(ctx), static_cast<uint8_t*>(ctx) + layout::kHitContextSize);
        calls.push_back(std::move(call));
    };
    return c;
}

template <typename T>
T field(const Call& c, size_t off) {
    T v;
    std::memcpy(&v, c.ctx.data() + off, sizeof(T));
    return v;
}

} // namespace

TEST(EldenRingHitContext, TemplateMustBeExactlyTheCapturedSize) {
    const std::vector<uint8_t> small(layout::kHitTemplateSize - 1, 0), big(layout::kHitTemplateSize + 1, 0);
    EXPECT_FALSE(HitTemplate::fromBytes(small.data(), small.size()).has_value());
    EXPECT_FALSE(HitTemplate::fromBytes(big.data(), big.size()).has_value());
    EXPECT_FALSE(HitTemplate::fromBytes(nullptr, layout::kHitTemplateSize).has_value());
}

TEST(EldenRingHitContext, FillsTheDocumentedFieldsAndKeepsTheRest) {
    const HitTemplate t = makeTemplate(0xAB);
    const float pos[3] = {13.f, 2.f, 24.f};
    const HitContext c = buildHitContext(t, 0x1111, 0x2222, 50, pos);
    Call call{0, 0, std::vector<uint8_t>(c.bytes, c.bytes + sizeof(c.bytes))};
    EXPECT_EQ(field<uint32_t>(call, layout::kHitDamage), 50u);
    EXPECT_EQ(field<uint8_t>(call, layout::kHitStaggerLevel), 3);
    EXPECT_EQ(field<uint8_t>(call, layout::kHitStaggerAnim), 2);
    EXPECT_EQ(field<uint8_t>(call, layout::kHitEnableReaction), 1);
    EXPECT_EQ(field<uint8_t>(call, layout::kHitHostileFlag), 1);
    EXPECT_EQ(field<uint64_t>(call, layout::kHitAttacker), 0x1111u);
    EXPECT_EQ(field<uint64_t>(call, layout::kHitVictim), 0x2222u);
    EXPECT_FLOAT_EQ(field<float>(call, layout::kHitPosition + 0), 13.f);
    EXPECT_FLOAT_EQ(field<float>(call, layout::kHitPosition + 4), 3.f); // +1 m: the waist
    EXPECT_FLOAT_EQ(field<float>(call, layout::kHitPosition + 8), 24.f);
    EXPECT_EQ(c.bytes[0], 0xAB);                         // template untouched elsewhere
    EXPECT_EQ(c.bytes[layout::kHitTemplateSize - 1], 0xAB);
    EXPECT_EQ(c.bytes[layout::kHitTemplateSize + 8], 0); // the tail beyond the template is zero (except +0x264)
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&c) % 16, 0u);
}

TEST(EldenRingDamage, AppliesOneCallThroughTheVictimsDamageModule) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    ASSERT_TRUE(q.enqueue(kEnemy, 50, 10));
    const auto res = q.drain(baseCtx(m, t, calls));
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].outcome, DamageOutcome::Applied);
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].module, kEnemy + 0x3800); // slot 19
    EXPECT_EQ(calls[0].attacker, kPlayer);
    EXPECT_EQ(field<uint32_t>(calls[0], layout::kHitDamage), 50u);
    EXPECT_EQ(field<uint64_t>(calls[0], layout::kHitVictim), kEnemy);
    EXPECT_EQ(field<uint8_t>(calls[0], layout::kHitEnableReaction), 1);
    EXPECT_EQ(q.pending(), 0u);
    // The request is gone: a second drain does not call again.
    EXPECT_TRUE(q.drain(baseCtx(m, t, calls)).empty());
    EXPECT_EQ(calls.size(), 1u);
}

TEST(EldenRingDamage, NothingRunsOffTheGameThread) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    q.enqueue(kEnemy, 50, 10);
    DrainContext c = baseCtx(m, t, calls);
    c.on_game_thread = false;
    EXPECT_TRUE(q.drain(c).empty());
    EXPECT_TRUE(calls.empty());
    EXPECT_EQ(q.pending(), 1u); // still queued for the right thread
}

TEST(EldenRingDamage, RefusesDeadNeutralAndWrongClassVictims) {
    Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    // dead
    m.put<int32_t>(kEnemy + 0x2000 + layout::kDataHp, 0);
    q.enqueue(kEnemy, 50, 10);
    auto r = q.drain(baseCtx(m, t, calls));
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::VictimDead);
    // neutral team
    m.put<int32_t>(kEnemy + 0x2000 + layout::kDataHp, 100);
    m.put<uint8_t>(kEnemy + layout::kTeamTypeInChrIns, 30);
    q.enqueue(kEnemy, 50, 10);
    r = q.drain(baseCtx(m, t, calls));
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::NotHostile);
    // damage module of another class (for example the player's)
    m.put<uint8_t>(kEnemy + layout::kTeamTypeInChrIns, 6);
    m.put<uint64_t>(kEnemy + 0x3800, kBase + 0x2A3A3E0);
    q.enqueue(kEnemy, 50, 10);
    r = q.drain(baseCtx(m, t, calls));
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::WrongModule);
    EXPECT_TRUE(calls.empty());
}

TEST(EldenRingDamage, RefusesGoneVictimsAndBadInputs) {
    Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    q.enqueue(0x7ff509000000ull, 50, 10); // unreadable
    q.enqueue(kPlayer, 50, 10);           // never the player itself
    q.enqueue(0, 50, 10);
    q.enqueue(kEnemy, 0, 10);             // zero damage
    q.enqueue(kEnemy, -5, 10);
    const auto r = q.drain(baseCtx(m, t, calls));
    ASSERT_EQ(r.size(), 5u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::VictimGone);
    EXPECT_EQ(r[1].outcome, DamageOutcome::VictimGone);
    EXPECT_EQ(r[2].outcome, DamageOutcome::VictimGone);
    EXPECT_EQ(r[3].outcome, DamageOutcome::BadDamage);
    EXPECT_EQ(r[4].outcome, DamageOutcome::BadDamage);
    EXPECT_TRUE(calls.empty());
}

TEST(EldenRingDamage, NoTemplateOrNoPlayerMeansNoCall) {
    Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    q.enqueue(kEnemy, 50, 10);
    DrainContext c = baseCtx(m, t, calls);
    c.hit_template = nullptr;
    auto r = q.drain(c);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::NoTemplate);
    m.put<uint64_t>(kWorld + layout::kPlayerInsInWorldChrMan, 0);
    q.enqueue(kEnemy, 50, 10);
    r = q.drain(baseCtx(m, t, calls));
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::NoPlayer);
    EXPECT_TRUE(calls.empty());
}

TEST(EldenRingDamage, OnlyTheEntityBeingUpdatedRunsAndOthersWait) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    q.enqueue(kEnemy, 50, 10);
    q.enqueue(kEnemy2, 40, 10);
    DrainContext c = baseCtx(m, t, calls);
    c.require_victim_updating = true;
    c.updating_data_module = kEnemy2 + 0x2000;
    auto r = q.drain(c);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].module, kEnemy2 + 0x3800);
    EXPECT_EQ(q.pending(), 1u); // kEnemy waits
    c.updating_data_module = kEnemy + 0x2000;
    r = q.drain(c);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(calls.size(), 2u);
    EXPECT_EQ(q.pending(), 0u);
    // Without knowing which entity is updating, nothing runs.
    q.enqueue(kEnemy, 50, 10);
    c.updating_data_module = 0;
    EXPECT_TRUE(q.drain(c).empty());
    EXPECT_EQ(q.pending(), 1u);
}

TEST(EldenRingDamage, DeadOrNeutralVictimsAreReportedEvenWhenAnotherEntityIsUpdating) {
    Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    m.put<int32_t>(kEnemy + 0x2000 + layout::kDataHp, 0); // killed earlier: its data module is no longer updated
    q.enqueue(kEnemy, 50, 10);
    DrainContext c = baseCtx(m, t, calls);
    c.require_victim_updating = true;
    c.updating_data_module = kEnemy2 + 0x2000;
    const auto r = q.drain(c);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::VictimDead); // not left to expire after 5 s
    EXPECT_EQ(q.pending(), 0u);
    EXPECT_TRUE(calls.empty());
}

TEST(EldenRingDamage, WaitingRequestsExpire) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    q.enqueue(kEnemy, 50, 10);
    DrainContext c = baseCtx(m, t, calls);
    c.require_victim_updating = true;
    c.updating_data_module = kEnemy2 + 0x2000;
    c.tick = 10 + 120; // exactly at the limit: still waiting
    EXPECT_TRUE(q.drain(c).empty());
    EXPECT_EQ(q.pending(), 1u);
    c.tick = 10 + 121;
    const auto r = q.drain(c);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].outcome, DamageOutcome::Expired);
    EXPECT_EQ(q.pending(), 0u);
    EXPECT_TRUE(calls.empty());
}

TEST(EldenRingDamage, LimitsWorkPerDrainAndKeepsTheRest) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    for (int i = 0; i < 6; ++i) q.enqueue(kEnemy, 10 + i, 10);
    DrainContext c = baseCtx(m, t, calls);
    c.max_per_drain = 4;
    EXPECT_EQ(q.drain(c).size(), 4u);
    EXPECT_EQ(q.pending(), 2u);
    EXPECT_EQ(q.drain(c).size(), 2u);
    ASSERT_EQ(calls.size(), 6u);
    // arrival order preserved
    for (int i = 0; i < 6; ++i) EXPECT_EQ(field<uint32_t>(calls[i], layout::kHitDamage), static_cast<uint32_t>(10 + i));
}

TEST(EldenRingDamage, ReentrantDrainIsRejectedAndDoesNotDoubleApply) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    DamageQueue q;
    q.enqueue(kEnemy, 50, 10);
    int calls = 0;
    DrainContext c;
    c.reader = &m;
    c.image_base = kBase;
    c.on_game_thread = true;
    c.tick = 10;
    c.hit_template = &t;
    c.require_victim_updating = false;
    c.invoke = [&](uintptr_t, uintptr_t, void*) {
        ++calls;
        // The game's own hit handling re-enters our hook while the call is running.
        EXPECT_TRUE(q.drain(c).empty());
    };
    EXPECT_EQ(q.drain(c).size(), 1u);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(q.reentrantRejections(), 1u);
    // After the outer drain finished, draining works again.
    q.enqueue(kEnemy, 50, 10);
    EXPECT_EQ(q.drain(c).size(), 1u);
    EXPECT_EQ(calls, 2);
}

TEST(EldenRingDamage, QueueIsBoundedAndThreadSafe) {
    DamageQueue q;
    for (size_t i = 0; i < DamageQueue::kCapacity; ++i) ASSERT_TRUE(q.enqueue(kEnemy, 1, 0));
    EXPECT_FALSE(q.enqueue(kEnemy, 1, 0));
    DamageQueue q2;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([&q2] {
            for (int i = 0; i < 100; ++i) q2.enqueue(kEnemy, 1, 0);
        });
    }
    for (auto& th : ts) th.join();
    EXPECT_EQ(q2.pending(), DamageQueue::kCapacity);
}

TEST(EldenRingDamage, RequestsEnqueuedDuringADrainAreNotLost) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    DamageQueue q;
    q.enqueue(kEnemy, 50, 10);
    DrainContext c;
    c.reader = &m;
    c.image_base = kBase;
    c.on_game_thread = true;
    c.tick = 10;
    c.hit_template = &t;
    c.require_victim_updating = false;
    c.invoke = [&](uintptr_t, uintptr_t, void*) { q.enqueue(kEnemy2, 30, 10); }; // a worker thread enqueues meanwhile
    EXPECT_EQ(q.drain(c).size(), 1u);
    EXPECT_EQ(q.pending(), 1u);
}

TEST(EldenRingDamage, OverrideFinalDamageOnlyForOurContext) {
    using namespace eldenring::live;
    uint8_t ctx[layout::kHitContextSize] = {};
    const uint64_t player = 0x1111, victim = 0x2222;
    std::memcpy(ctx + layout::kHitAttacker, &player, 8);
    std::memcpy(ctx + layout::kHitVictim, &victim, 8);
    const int32_t engine = 63;
    std::memcpy(ctx + layout::kHitDamage, &engine, 4);

    int32_t seen = -1;
    EXPECT_TRUE(overrideFinalDamage(ctx, player, victim, 5, &seen));
    EXPECT_EQ(seen, 63);
    int32_t now = 0;
    std::memcpy(&now, ctx + layout::kHitDamage, 4);
    EXPECT_EQ(now, 5);

    // another victim (a natural hit the game processes on the same thread): untouched
    std::memcpy(ctx + layout::kHitDamage, &engine, 4);
    EXPECT_FALSE(overrideFinalDamage(ctx, player, 0x3333, 5, &seen));
    EXPECT_FALSE(overrideFinalDamage(ctx, 0x4444, victim, 5, &seen));
    std::memcpy(&now, ctx + layout::kHitDamage, 4);
    EXPECT_EQ(now, 63);

    EXPECT_FALSE(overrideFinalDamage(ctx, player, victim, 0, &seen));  // nothing to force
    EXPECT_FALSE(overrideFinalDamage(nullptr, player, victim, 5, &seen));
}

TEST(EldenRingDamage, RequestTagSurvivesToTheResult) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    ASSERT_TRUE(q.enqueue(kEnemy, 16, 10, 1u));
    const auto res = q.drain(baseCtx(m, t, calls));
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].outcome, DamageOutcome::Applied);
    EXPECT_EQ(res[0].request.tag, 1u);
    EXPECT_EQ(res[0].request.base_damage, 16);
}

TEST(EldenRingDamage, AWallBetweenPlayerAndVictimStopsTheHit) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    q.enqueue(kEnemy, 11, 10);
    DrainContext c = baseCtx(m, t, calls);
    int asked = 0;
    c.line_of_sight = [&](const float*, const float*) {
        ++asked;
        return false;
    };
    const auto res = q.drain(c);
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].outcome, DamageOutcome::NoLineOfSight);
    EXPECT_EQ(asked, 1);
    EXPECT_TRUE(calls.empty());   // the game was never called
    EXPECT_EQ(q.pending(), 0u);   // and the request is dropped, not retried
}

TEST(EldenRingDamage, ClearLineOfSightStillHits) {
    const Mem m = makeWorld();
    const HitTemplate t = makeTemplate();
    std::vector<Call> calls;
    DamageQueue q;
    q.enqueue(kEnemy, 11, 10);
    DrainContext c = baseCtx(m, t, calls);
    c.line_of_sight = [](const float*, const float*) { return true; };
    const auto res = q.drain(c);
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].outcome, DamageOutcome::Applied);
    EXPECT_EQ(calls.size(), 1u);
}
