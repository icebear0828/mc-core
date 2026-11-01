// Tests for ProjectileService: the ranged half extracted from CombatService (bullet template cache,
// spawn/hit hooks, bullet firing and the diagnostic AimCalibrator). All game memory is faked through
// CombatCallbacks; no Windows game code runs.

#include <gtest/gtest.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "eldenring_projectile_service.hpp"
#include "eldenring_runtime_context.hpp"

using namespace eldenring::combat;
using namespace eldenring::live;
using eldenring::RuntimeContext;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr uintptr_t kPlayer = 0x7ff500100000ull;

class FakeMem : public IMemoryReader {
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
};

// A request body that templateUsable() accepts: param row > 100000 and DummyPoly 56.
std::vector<uint8_t> makeUsableRequest(uint64_t owner, int32_t row = 0x06455A50, int32_t poly = 56) {
    std::vector<uint8_t> req(eldenring::bullet::kFullRequestBytes, 0);
    std::memcpy(req.data() + 0x00, &owner, sizeof(owner));
    std::memcpy(req.data() + 0x08, &row, sizeof(row));
    std::memcpy(req.data() + 0x1C, &poly, sizeof(poly));
    return req;
}

void setBodyAndCamera(CombatCallbacks& cb, uintptr_t player) {
    cb.get_player_chr = [player]() { return player; };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.position[0] = 0.f; cam.position[1] = 1.6f; cam.position[2] = 0.f;
        return true;
    };
    cb.read_physics_orient = [](uintptr_t, float q[4]) {
        q[0] = 0.f; q[1] = 0.f; q[2] = 0.f; q[3] = 1.f;
        return true;
    };
}

} // namespace

TEST(EldenRingProjectileService, DefaultTemplateLoadsWhenFileIsMissing) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    svc.loadBulletTemplate("");
    ASSERT_TRUE(svc.hasValidBulletTemplate());

    uint8_t out[eldenring::bullet::kFullRequestBytes] = {};
    svc.getBulletTemplate(out);
    EXPECT_TRUE(eldenring::bullet::templateUsable(out, sizeof(out)));
    EXPECT_EQ(std::memcmp(out, eldenring::bullet::kDefaultBoltTemplate, sizeof(out)), 0);
}

TEST(EldenRingProjectileService, RecordBulletTemplateRequiresPlayerOwner) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    FakeMem mem;
    const uint64_t handle = 0xABCDEF;
    mem.region(kPlayer, 0x100);
    mem.put<uint64_t>(kPlayer + 8, handle);

    const std::vector<uint8_t> foreign = makeUsableRequest(handle + 1);
    mem.region(0x9000, foreign.size());
    mem.regions[0x9000] = foreign;

    CombatCallbacks cb{};
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.get_player_chr = []() { return kPlayer; };
    svc.setCallbacks(cb);

    svc.recordBulletTemplate(reinterpret_cast<const void*>(0x9000));
    EXPECT_FALSE(svc.hasValidBulletTemplate());

    const std::vector<uint8_t> mine = makeUsableRequest(handle);
    mem.regions[0x9000] = mine;
    svc.recordBulletTemplate(reinterpret_cast<const void*>(0x9000));
    ASSERT_TRUE(svc.hasValidBulletTemplate());

    uint8_t out[eldenring::bullet::kFullRequestBytes] = {};
    svc.getBulletTemplate(out);
    EXPECT_EQ(std::memcmp(out, mine.data(), mine.size()), 0);
}

TEST(EldenRingProjectileService, RecordBulletTemplateRejectsUnusableBody) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    FakeMem mem;
    const uint64_t handle = 0xABCDEF;
    mem.region(kPlayer, 0x100);
    mem.put<uint64_t>(kPlayer + 8, handle);

    // Row <= 100000 is a summon/SpEffect bullet, not a ranged weapon.
    const std::vector<uint8_t> bad = makeUsableRequest(handle, 1501, 56);
    mem.region(0x9000, bad.size());
    mem.regions[0x9000] = bad;

    CombatCallbacks cb{};
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.get_player_chr = []() { return kPlayer; };
    svc.setCallbacks(cb);

    svc.recordBulletTemplate(reinterpret_cast<const void*>(0x9000));
    EXPECT_FALSE(svc.hasValidBulletTemplate());
}

TEST(EldenRingProjectileService, OnSpawnBulletCapturesIdTimeAndPosition) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    FakeMem mem;
    std::vector<uint8_t> req(eldenring::bullet::kFullRequestBytes, 0);
    const uint32_t id = eldenring::bullet::kBoltBulletId;
    std::memcpy(req.data() + 0x1C, &id, sizeof(id));
    const float pos[3] = {1.f, 2.f, 3.f};
    std::memcpy(req.data() + 0x80, pos, sizeof(pos));
    mem.region(0xB000, req.size());
    mem.regions[0xB000] = req;

    CombatCallbacks cb{};
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    svc.setCallbacks(cb);

    svc.onSpawnBullet(reinterpret_cast<const void*>(0xB000));

    EXPECT_EQ(svc.lastBulletId(), id);
    EXPECT_FLOAT_EQ(svc.lastShotDamage(), eldenring::bullet::kCrossbowDamage);
    EXPECT_GT(svc.lastShotMs(), 0u);
    float out[3] = {};
    svc.getLastShotPos(out);
    EXPECT_FLOAT_EQ(out[0], 1.f);
    EXPECT_FLOAT_EQ(out[1], 2.f);
    EXPECT_FLOAT_EQ(out[2], 3.f);
}

TEST(EldenRingProjectileService, HandleProjectileHitOverridesDamageForPlayerBolt) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    svc.setArrowDamage(true);

    FakeMem mem;
    const uintptr_t kVictim = 0x9000;
    const uintptr_t kModule = 0x9800;
    const uintptr_t kHitCtx = 0xA000;
    mem.region(kVictim, 0x100);
    mem.region(kModule, 0x100);
    mem.region(kHitCtx, 0x300);
    mem.put<uint64_t>(kModule + layout::kOwnerInDataModule, kVictim);

    const uint8_t kind = 2;
    const int32_t engine_damage = 35;
    mem.put<uint8_t>(kHitCtx + 0xDA, kind);
    mem.put<uint64_t>(kHitCtx + layout::kHitAttacker, kPlayer);
    mem.put<uint64_t>(kHitCtx + layout::kHitVictim, kVictim);
    mem.put<int32_t>(kHitCtx + layout::kHitDamage, engine_damage);

    std::string played_sound;
    bool override_called = false;
    int override_val = 0;
    CombatCallbacks cb{};
    cb.get_player_chr = []() { return kPlayer; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.read_vitals = [&](uintptr_t chr, Vitals& v) {
        if (chr == kVictim) { v.hp = 100; v.max_hp = 100; return true; }
        return false;
    };
    cb.play_sound = [&](const char* snd, float) { played_sound = snd; };
    cb.override_final_damage = [&](uint8_t*, uintptr_t, uintptr_t, int val, int* eng_val) {
        override_called = true;
        override_val = val;
        *eng_val = engine_damage;
        return true;
    };
    svc.setCallbacks(cb);

    // Record a recent bolt spawn so isPlayersBoltHit() accepts the hit.
    std::vector<uint8_t> spawn_req(eldenring::bullet::kFullRequestBytes, 0);
    const uint32_t bolt_id = eldenring::bullet::kBoltBulletId;
    std::memcpy(spawn_req.data() + 0x1C, &bolt_id, sizeof(bolt_id));
    mem.region(0xB000, spawn_req.size());
    mem.regions[0xB000] = spawn_req;
    svc.onSpawnBullet(reinterpret_cast<const void*>(0xB000));

    HitFeedback feedback;
    svc.handleProjectileHit(reinterpret_cast<void*>(kModule), reinterpret_cast<void*>(kPlayer),
                            reinterpret_cast<uint8_t*>(kHitCtx), feedback, ctx);

    EXPECT_EQ(played_sound, "entity.arrow.hit");
    EXPECT_TRUE(override_called);
    EXPECT_EQ(override_val, eldenring::bullet::boltDamageEr(100, eldenring::bullet::kCrossbowDamage));
}

TEST(EldenRingProjectileService, HandleProjectileHitIgnoresForeignShots) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    svc.setArrowDamage(true);

    FakeMem mem;
    const uintptr_t kVictim = 0x9000;
    const uintptr_t kModule = 0x9800;
    const uintptr_t kHitCtx = 0xA000;
    mem.region(kVictim, 0x100);
    mem.region(kModule, 0x100);
    mem.region(kHitCtx, 0x300);
    mem.put<uint64_t>(kModule + layout::kOwnerInDataModule, kVictim);
    // kind 3 (not a bolt) and a foreign attacker.
    const uint8_t kind = 3;
    mem.put<uint8_t>(kHitCtx + 0xDA, kind);
    mem.put<uint64_t>(kHitCtx + layout::kHitAttacker, 0xDEADBEEF);
    mem.put<uint64_t>(kHitCtx + layout::kHitVictim, kVictim);
    mem.put<int32_t>(kHitCtx + layout::kHitDamage, 35);

    bool override_called = false;
    CombatCallbacks cb{};
    cb.get_player_chr = []() { return kPlayer; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.read_vitals = [&](uintptr_t chr, Vitals& v) {
        if (chr == kVictim) { v.hp = 100; v.max_hp = 100; return true; }
        return false;
    };
    cb.override_final_damage = [&](uint8_t*, uintptr_t, uintptr_t, int, int*) {
        override_called = true;
        return true;
    };
    svc.setCallbacks(cb);

    HitFeedback feedback;
    svc.handleProjectileHit(reinterpret_cast<void*>(kModule), reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEF)),
                            reinterpret_cast<uint8_t*>(kHitCtx), feedback, ctx);
    EXPECT_FALSE(override_called);
}

TEST(EldenRingProjectileService, QueueBulletFireRequiresTemplateAndThrottles) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    svc.setBulletFireEnabled(true);

    EXPECT_FALSE(svc.queueBulletFire(9.0f, ctx));

    svc.loadBulletTemplate("");
    ASSERT_TRUE(svc.hasValidBulletTemplate());

    EXPECT_TRUE(svc.queueBulletFire(9.0f, ctx));
    EXPECT_TRUE(ctx.mode().bullet_fire_pending.load());
    EXPECT_FALSE(svc.queueBulletFire(9.0f, ctx));
}

TEST(EldenRingProjectileService, RunBulletFireBuildsRequestAndCallsSpawn) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    svc.loadBulletTemplate("");
    ASSERT_TRUE(svc.hasValidBulletTemplate());

    const uintptr_t manager = 0x5000;
    const uint64_t handle = 0xABCDEF;
    FakeMem mem;
    mem.region(kBase + 0x3D667A8, 8);
    mem.put<uint64_t>(kBase + 0x3D667A8, manager);
    mem.region(kPlayer, 0x100);
    mem.put<uint64_t>(kPlayer + 8, handle);

    std::vector<uint8_t> captured;
    bool our_fire_during_spawn = false;
    CombatCallbacks cb{};
    cb.get_player_chr = []() { return kPlayer; };
    cb.image_base = kBase;
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.position[0] = 0.f; cam.position[1] = 1.6f; cam.position[2] = 0.f;
        return true;
    };
    cb.spawn_bullet = [&](void*, uint32_t* out_handle, const void* req, void*) {
        our_fire_during_spawn = svc.isOurBulletFire();
        const uint8_t* p = static_cast<const uint8_t*>(req);
        captured.assign(p, p + eldenring::bullet::kFullRequestBytes);
        *out_handle = 0x1234u;
        return true;
    };
    svc.setCallbacks(cb);

    svc.runBulletFire(ctx);

    ASSERT_EQ(captured.size(), eldenring::bullet::kFullRequestBytes);
    uint64_t owner = 0, row_id = 0;
    uint32_t flags = 0;
    int32_t poly = 0;
    float pos[3] = {};
    std::memcpy(&owner, captured.data() + 0x00, sizeof(owner));
    std::memcpy(&row_id, captured.data() + 0x08, sizeof(row_id));
    std::memcpy(&flags, captured.data() + 0x44, sizeof(flags));
    std::memcpy(&poly, captured.data() + 0x1C, sizeof(poly));
    std::memcpy(pos, captured.data() + 0x80, sizeof(pos));

    EXPECT_EQ(owner, handle);
    EXPECT_EQ(static_cast<uint32_t>(row_id), 0x06455A50u);
    EXPECT_EQ(static_cast<uint32_t>(row_id >> 32), 0xFFFFFFFFu);
    EXPECT_NE(flags & 0x08u, 0u);
    EXPECT_EQ(flags & 0x02u, 0u);
    EXPECT_EQ(poly, -1); // default bolt_attach_poly keeps the "no attachment point" request
    EXPECT_FLOAT_EQ(pos[0], 0.f);
    EXPECT_FLOAT_EQ(pos[1], 1.6f);
    EXPECT_FLOAT_EQ(pos[2], 0.8f); // muzzle = camera position + forward * 0.8
    EXPECT_TRUE(our_fire_during_spawn);
    EXPECT_FALSE(svc.isOurBulletFire());
}

TEST(EldenRingProjectileService, RunBulletFireWithoutTemplateDoesNotCallSpawn) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();

    int spawn_calls = 0;
    CombatCallbacks cb{};
    cb.spawn_bullet = [&](void*, uint32_t*, const void*, void*) {
        ++spawn_calls;
        return true;
    };
    svc.setCallbacks(cb);

    svc.runBulletFire(ctx);
    EXPECT_EQ(spawn_calls, 0);
}

TEST(EldenRingProjectileService, AimCalStartCapturesIsolationWithInjectedClock) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    uint64_t now = 1000000000000ull; // dominate any real GetTickCount64 value from earlier tests
    svc.aimCalibrator().setClock([&]() { return now; });
    svc.setAimCalEnabled(true);

    CombatCallbacks cb{};
    setBodyAndCamera(cb, kPlayer);
    svc.setCallbacks(cb);

    const float dir[3] = {0.f, 0.f, 1.f};
    svc.aimCalStart(0x5000, 0x1234, dir);
    const AimCalState& s1 = svc.aimCalState();
    EXPECT_TRUE(s1.active);
    EXPECT_TRUE(s1.isolated);
    EXPECT_TRUE(s1.have_cam);
    EXPECT_TRUE(s1.have_body);
    EXPECT_EQ(s1.handle, 0x1234u);

    now += 3000;
    svc.aimCalStart(0x5000, 0x1235, dir);
    EXPECT_FALSE(svc.aimCalState().isolated);

    now += 5000; // 8 s after the first start, 5 s after the second
    svc.aimCalStart(0x5000, 0x1236, dir);
    EXPECT_TRUE(svc.aimCalState().isolated);
}

TEST(EldenRingProjectileService, AimCalTickSamplesAt250msAndStops) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    uint64_t now = 1000000000000ull;
    svc.aimCalibrator().setClock([&]() { return now; });
    svc.setAimCalEnabled(true);

    const uintptr_t manager = 0x5000;
    const uintptr_t obj = 0x6000;
    FakeMem mem;
    mem.region(manager, 8);
    mem.put<uint64_t>(manager, obj);
    mem.region(obj, 0x40);
    const float pos[3] = {0.f, 0.f, 1.f};
    for (int i = 0; i < 3; ++i) mem.put<float>(obj + 0x10 + 4 * i, pos[i]);
    const float q[4] = {0.f, 0.f, 0.f, 1.f};
    for (int i = 0; i < 4; ++i) mem.put<float>(obj + 0x20 + 4 * i, q[i]);

    std::vector<std::string> logs;
    CombatCallbacks cb{};
    setBodyAndCamera(cb, kPlayer);
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.read(a, d, s); };
    cb.log = [&](const std::string& line) { logs.push_back(line); };
    svc.setCallbacks(cb);

    const float dir[3] = {0.f, 0.f, 1.f};
    svc.aimCalStart(manager, 0x1234, dir);
    ASSERT_TRUE(svc.aimCalState().active);

    now += 250; svc.aimCalTick();
    EXPECT_EQ(svc.aimCalState().samples, 1);
    now += 250; svc.aimCalTick();
    EXPECT_EQ(svc.aimCalState().samples, 2);
    EXPECT_TRUE(svc.aimCalState().have2);

    // Move the bullet so the sample-4 flight vector is non-zero.
    const float pos4[3] = {0.f, 0.f, 2.f};
    for (int i = 0; i < 3; ++i) mem.put<float>(obj + 0x10 + 4 * i, pos4[i]);

    now += 250; svc.aimCalTick();
    EXPECT_EQ(svc.aimCalState().samples, 3);
    now += 250; svc.aimCalTick();
    EXPECT_EQ(svc.aimCalState().samples, 4);
    EXPECT_FALSE(svc.aimCalState().active);

    bool saw_aimcal = false, saw_yaws = false;
    for (const std::string& line : logs) {
        if (line.rfind("AIMCAL:", 0) == 0) saw_aimcal = true;
        if (line.rfind("AIMCAL-YAWS:", 0) == 0) saw_yaws = true;
    }
    EXPECT_TRUE(saw_aimcal);
    EXPECT_TRUE(saw_yaws);
}

TEST(EldenRingProjectileService, AimCalDisabledDoesNotStart) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    CombatCallbacks cb{};
    setBodyAndCamera(cb, kPlayer);
    // No file_exists callback and aim calibration disabled -> the probe stays inactive.
    svc.setCallbacks(cb);

    const float dir[3] = {0.f, 0.f, 1.f};
    svc.aimCalStart(0x5000, 0x1234, dir);
    EXPECT_FALSE(svc.aimCalState().active);
}

TEST(EldenRingProjectileService, ResetForTestingClearsProjectileState) {
    auto& svc = ProjectileService::Instance();
    svc.resetForTesting();

    svc.setBulletFireEnabled(true);
    svc.setArrowDamage(true);
    svc.setProjLog(true);
    svc.setBoltAttachPoly(56);
    svc.setAimCalEnabled(true);
    svc.loadBulletTemplate("");
    ASSERT_TRUE(svc.hasValidBulletTemplate());

    svc.resetForTesting();

    EXPECT_FALSE(svc.bulletFireEnabled());
    EXPECT_FALSE(svc.arrowDamage());
    EXPECT_FALSE(svc.projLog());
    EXPECT_EQ(svc.boltAttachPoly(), -1);
    EXPECT_FALSE(svc.aimCalEnabled());
    EXPECT_FALSE(svc.hasValidBulletTemplate());
    EXPECT_FALSE(svc.aimCalState().active);
    EXPECT_FLOAT_EQ(svc.lastShotDamage(), eldenring::bullet::kCrossbowDamage);
}
