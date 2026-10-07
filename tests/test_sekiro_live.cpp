#include <gtest/gtest.h>

#include "sekiro_live.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

using namespace sekiro::live;
using sekiro::native::ChrCam;
using sekiro::native::ChrIns;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr size_t kImageSize = 0x4000;
constexpr uintptr_t kWorld = 0x7ff400000000ull;   // WorldChrMan object
constexpr uintptr_t kPlayer = 0x7ff400100000ull;  // player ChrIns
constexpr uintptr_t kCamera = 0x7ff400200000ull;  // camera object
constexpr uintptr_t kCameraDecoy = 0x7ff400300000ull;
constexpr uintptr_t kChrModel = 0x7ff400400000ull;  // player ChrIns+0x48 object
constexpr uintptr_t kModuleContainer = 0x7ff400500000ull; // [player+0x10b8]
constexpr uintptr_t kDataModule = 0x7ff400600000ull;      // [container+0x1e8], SprjChrDataModule
constexpr uint32_t kDataModuleVtableRva = 0x2A8BE18;
constexpr uintptr_t kBlock = 0x7ff400700000ull;       // [WorldChrMan+0xC8] WorldBlockChr
constexpr uintptr_t kSlots = 0x7ff400710000ull;       // slot array, stride 0x38
constexpr uintptr_t kEnemyBase = 0x7ff400800000ull;   // enemies are kEnemyBase + n * kEnemyStride
constexpr uintptr_t kEnemyStride = 0x20000;
constexpr uint32_t kBlockVtableRva = 0x2A2EB10;
constexpr uintptr_t kFallModule = 0x7ff400900000ull; // [container+0x240], SprjPlayerFallModule
constexpr uint32_t kFallModuleVtableRva = 0x2A821F0;
constexpr uint32_t kEnemyVtableRva = 0x2A27F28;

constexpr uint32_t kWcmPatternRva = 0x100;
constexpr uint32_t kWcmGlobalRva = 0x1000;
constexpr uint32_t kCamPatternRva = 0x200;
constexpr uint32_t kCamGlobalRva = 0x1008;
constexpr uint32_t kCamDecoyPatternRva = 0x300;
constexpr uint32_t kCamDecoyGlobalRva = 0x1010;

// Sparse fake address space: reads must fall entirely inside one region.
class FakeMemory : public IMemoryReader {
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
    std::vector<uint8_t>& region(uintptr_t start, size_t size) {
        auto& r = regions[start];
        if (r.size() < size) r.resize(size, 0);
        return r;
    }
    template <typename T>
    void put(uintptr_t address, const T& value) {
        for (auto& [start, bytes] : regions) {
            if (address >= start && address + sizeof(T) <= start + bytes.size()) {
                std::memcpy(bytes.data() + (address - start), &value, sizeof(T));
                return;
            }
        }
        FAIL() << "address not in any region";
    }
    void putBytes(uintptr_t address, const std::vector<uint8_t>& v) {
        for (auto& [start, bytes] : regions) {
            if (address >= start && address + v.size() <= start + bytes.size()) {
                std::memcpy(bytes.data() + (address - start), v.data(), v.size());
                return;
            }
        }
        FAIL() << "address not in any region";
    }
};

// mov reg,[rip+rel32] style instruction followed by a fixed tail
std::vector<uint8_t> ripLoad(std::vector<uint8_t> head3, uint32_t at_rva, uint32_t global_rva, std::vector<uint8_t> tail) {
    const int32_t rel = static_cast<int32_t>(global_rva) - static_cast<int32_t>(at_rva + 7);
    std::vector<uint8_t> v = std::move(head3);
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((rel >> (8 * i)) & 0xff));
    v.insert(v.end(), tail.begin(), tail.end());
    return v;
}

void writeWcmCode(FakeMemory& m, uint32_t at = kWcmPatternRva, uint32_t global = kWcmGlobalRva) {
    m.putBytes(kBase + at, ripLoad({0x48, 0x8B, 0x35}, at, global, {0x44, 0x0F, 0x28, 0x18}));
}
void writeCamCode(FakeMemory& m, uint32_t at, uint32_t global) {
    m.putBytes(kBase + at, ripLoad({0x48, 0x8B, 0x05}, at, global,
                                   {0x48, 0x85, 0xC0, 0x74, 0x09, 0xF3, 0x0F, 0x10, 0x80, 0x60, 0x01, 0x00, 0x00, 0xC3}));
}

struct Mat {
    float rows[4][4];
};
Mat identityCamera(float px, float py, float pz) {
    Mat m{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {px, py, pz, 1}}};
    return m;
}
// Camera yawed so that forward = (sin a, 0, cos a)
Mat yawedCamera(float a, float px, float py, float pz) {
    Mat m{{{std::cos(a), 0, -std::sin(a), 0}, {0, 1, 0, 0}, {std::sin(a), 0, std::cos(a), 0}, {px, py, pz, 1}}};
    return m;
}

struct World {
    FakeMemory mem;

    World() {
        mem.region(kBase, kImageSize);
        mem.region(kWorld, 0x200);
        mem.region(kPlayer, 0x2000);
        mem.region(kCamera, 0x1000);
        mem.region(kCameraDecoy, 0x1000);
        writeWcmCode(mem);
        writeCamCode(mem, kCamPatternRva, kCamGlobalRva);
        setPlayer(10.f, -36.f, 5.f);
        setCamera(identityCamera(8.f, -34.f, 2.f));
        mem.put<uint64_t>(kBase + kWcmGlobalRva, kWorld);
        mem.put<uint64_t>(kWorld + 0x88, kPlayer);
        mem.put<uint64_t>(kBase + kCamGlobalRva, kCamera);
    }
    void setPlayer(float x, float y, float z, float copy_dx = 0.f) {
        float a[3] = {x, y, z};
        float b[3] = {x + copy_dx, y, z};
        mem.put(kPlayer + 0x1050, a);
        mem.put(kPlayer + 0x1060, b);
    }
    // The model object holds the character's world transform at +0x30: a row-major 3x4 [R | t] with R a
    // rotation about Y and t the player's position (measured on the real game). The model faces -Z.
    // `yaw` is the heading of the character in native (x, z) = (sin yaw, cos yaw).
    void setModelTransform(float yaw, float px, float py, float pz, bool break_translation = false) {
        if (!mem.regions.count(kChrModel)) mem.region(kChrModel, 0x400);
        mem.put<uint64_t>(kPlayer + 0x48, kChrModel);
        const float c = std::cos(yaw), s = std::sin(yaw);
        // forward = (-R02, -R22) = (sin yaw, cos yaw)  =>  R02 = -sin, R22 = -cos; keep R a proper rotation.
        const float m[12] = {-c, 0.f, -s, break_translation ? px + 50.f : px,
                             0.f, 1.f, 0.f, py,
                             s, 0.f, -c, pz};
        mem.put(kChrModel + 0x30, m);
    }
    // Player vitals as measured on the real game: ChrIns+0x10b8 -> container +0x1e8 -> SprjChrDataModule,
    // hp at +0x130, max at +0x138, identified by its vtable.
    void setVitals(int32_t hp, int32_t max_hp, bool right_class = true) {
        if (!mem.regions.count(kModuleContainer)) mem.region(kModuleContainer, 0x400);
        if (!mem.regions.count(kDataModule)) mem.region(kDataModule, 0x400);
        mem.put<uint64_t>(kPlayer + 0x10b8, kModuleContainer);
        mem.put<uint64_t>(kModuleContainer + 0x1e8, kDataModule);
        mem.put<uint64_t>(kDataModule, kBase + (right_class ? kDataModuleVtableRva : 0x1234));
        mem.put<int32_t>(kDataModule + 0x130, hp);
        mem.put<int32_t>(kDataModule + 0x138, max_hp);
    }
    // Fall state as measured on the real game: [[player+0x10b8]+0x240] is SprjPlayerFallModule; its int32 at +0x40
    // is -1 while standing or running and >= 0 while airborne.
    void setFallState(int32_t state, bool right_class = true) {
        if (!mem.regions.count(kModuleContainer)) mem.region(kModuleContainer, 0x400);
        if (!mem.regions.count(kFallModule)) mem.region(kFallModule, 0x100);
        mem.put<uint64_t>(kPlayer + 0x10b8, kModuleContainer);
        mem.put<uint64_t>(kModuleContainer + 0x240, kFallModule);
        mem.put<uint64_t>(kFallModule, kBase + (right_class ? kFallModuleVtableRva : 0x4242));
        mem.put<int32_t>(kFallModule + 0x40, state);
    }
    // ---- enemies: WorldChrMan+0xC8 -> block (+0x80 slot count, +0x88 slot array, stride 0x38, enemy at +0) ----
    struct EnemySpec {
        float x{0}, y{0}, z{0};
        float live_y{std::numeric_limits<float>::quiet_NaN()};
        uint32_t char_id{10010000};
        uint32_t team{5};
        int32_t hp{2101};
        int32_t max_hp{2101};
        bool right_class{true};
        bool module_ok{true};
        bool active{true}; // live position (+0x1050) non-zero; inactive ones only have a spawn position (+0xE0)
    };
    void ensureBlock(int32_t slots = 144) {
        if (!mem.regions.count(kBlock)) mem.region(kBlock, 0x200);
        if (!mem.regions.count(kSlots)) mem.region(kSlots, 0x38 * 256);
        mem.put<uint64_t>(kWorld + 0xC8, kBlock);
        mem.put<uint64_t>(kBlock, kBase + kBlockVtableRva);
        mem.put<int32_t>(kBlock + 0x80, slots);
        mem.put<uint64_t>(kBlock + 0x88, kSlots);
    }
    uintptr_t addEnemy(int slot, const EnemySpec& e) {
        ensureBlock();
        const uintptr_t enemy = kEnemyBase + static_cast<uintptr_t>(slot) * kEnemyStride;
        const uintptr_t container = enemy + 0x10000;
        const uintptr_t module = enemy + 0x11000;
        mem.region(enemy, 0x20000);
        mem.put<uint64_t>(kSlots + static_cast<uintptr_t>(slot) * 0x38, enemy);
        mem.put<uint64_t>(enemy, kBase + (e.right_class ? kEnemyVtableRva : 0x4321));
        mem.put<uint32_t>(enemy + 0x68, e.char_id);
        mem.put<uint32_t>(enemy + 0x70, e.team);
        const float spawn[3] = {e.x, e.y, e.z};
        mem.put(enemy + 0xE0, spawn);
        if (e.active) {
            const float live[3] = {e.x, std::isnan(e.live_y) ? e.y : e.live_y, e.z};
            mem.put(enemy + 0x1050, live);
            mem.put(enemy + 0x1060, live);
        }
        mem.put<uint64_t>(enemy + 0x10b8, container);
        mem.put<uint64_t>(container + 0x1f8, module);
        mem.put<uint64_t>(module, kBase + (e.module_ok ? kDataModuleVtableRva : 0x999));
        mem.put<int32_t>(module + 0x130, e.hp);
        mem.put<int32_t>(module + 0x160, e.max_hp);
        return enemy;
    }
    void setCamera(const Mat& m, uintptr_t obj = kCamera) { mem.put(obj + 0xea0, m); }
};

} // namespace

// ----------------------------------------------------------------------------- pattern

TEST(SekiroLivePatternTest, ParsesWildcardsAndRejectsGarbage) {
    const Pattern p = Pattern::parse("48 8B ?? 05 ? FF");
    ASSERT_EQ(p.bytes.size(), 6u);
    EXPECT_EQ(p.mask, (std::vector<uint8_t>{1, 1, 0, 1, 0, 1}));
    EXPECT_EQ(p.bytes[0], 0x48);
    EXPECT_EQ(p.bytes[5], 0xFF);
    EXPECT_TRUE(Pattern::parse("ZZ 10").bytes.empty()); // invalid token => empty (never matches)
}

TEST(SekiroLivePatternTest, FindsMatchesAcrossChunkBoundaries) {
    FakeMemory mem;
    mem.region(kBase, 256);
    mem.putBytes(kBase + 30, {0xAA, 0xBB, 0xCC, 0xDD});  // straddles chunk 32
    mem.putBytes(kBase + 100, {0xAA, 0x00, 0xCC, 0xDD});
    const auto hits = findPattern(mem, kBase, 256, Pattern::parse("AA ?? CC DD"), /*chunk=*/32);
    ASSERT_TRUE(hits.has_value());
    EXPECT_EQ(*hits, (std::vector<uint32_t>{30, 100}));
}

TEST(SekiroLivePatternTest, UnreadableImageIsNotTreatedAsNoMatch) {
    FakeMemory mem; // nothing mapped
    EXPECT_FALSE(findPattern(mem, kBase, 256, Pattern::parse("AA"), 64).has_value());
}

// ----------------------------------------------------------------------------- binding

TEST(SekiroLiveBinderTest, EncryptedCodeKeepsSearching) {
    FakeMemory mem;
    mem.region(kBase, kImageSize);
    for (size_t i = 0; i < kImageSize; ++i) mem.regions[kBase][i] = static_cast<uint8_t>((i * 131 + 7) & 0xff); // noise
    LiveBinder binder(mem, kBase, kImageSize);
    EXPECT_EQ(binder.scan(), BindStatus::Searching);
    LiveSample s;
    EXPECT_EQ(binder.sample(s), SampleStatus::NotBound);
}

TEST(SekiroLiveBinderTest, BindsOnceCodeIsDecryptedAndResolvesRipRelativeGlobals) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    EXPECT_EQ(binder.scan(), BindStatus::Bound);
    EXPECT_EQ(binder.worldChrManGlobalRva(), kWcmGlobalRva);
    ASSERT_EQ(binder.cameraCandidateRvas().size(), 1u);
    EXPECT_EQ(binder.cameraCandidateRvas()[0], kCamGlobalRva);
}

TEST(SekiroLiveBinderTest, DuplicateWorldChrManMatchIsAmbiguousNotGuessed) {
    World w;
    writeWcmCode(w.mem, 0x180, 0x1018); // a second, different global
    LiveBinder binder(w.mem, kBase, kImageSize);
    EXPECT_EQ(binder.scan(), BindStatus::Ambiguous);
    LiveSample s;
    EXPECT_EQ(binder.sample(s), SampleStatus::NotBound);
}

TEST(SekiroLiveBinderTest, NeedsCameraCandidateToBind) {
    World w;
    w.mem.putBytes(kBase + kCamPatternRva, std::vector<uint8_t>(32, 0x90)); // erase camera code
    LiveBinder binder(w.mem, kBase, kImageSize);
    EXPECT_EQ(binder.scan(), BindStatus::Searching);
}

// ----------------------------------------------------------------------------- sampling

TEST(SekiroLiveSampleTest, ReadsPlayerPositionAndCameraBasisInNativeMetres) {
    World w;
    w.setCamera(yawedCamera(0.5f, 8.f, -34.f, 2.f));
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);

    LiveSample s;
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FLOAT_EQ(s.player_pos.X, 10.f);
    EXPECT_FLOAT_EQ(s.player_pos.Y, -36.f);
    EXPECT_FLOAT_EQ(s.player_pos.Z, 5.f);
    EXPECT_NEAR(s.cam_forward.X, std::sin(0.5f), 1e-6f);
    EXPECT_NEAR(s.cam_forward.Z, std::cos(0.5f), 1e-6f);
    EXPECT_NEAR(s.cam_right.X, std::cos(0.5f), 1e-6f);
    EXPECT_FLOAT_EQ(s.cam_up.Y, 1.f);
    EXPECT_FLOAT_EQ(s.cam_pos.X, 8.f);
    EXPECT_FLOAT_EQ(s.cam_pos.Y, -34.f);
}

TEST(SekiroLiveSampleTest, ReadsVerticalFovWhenPlausibleAndReportsZeroOtherwise) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;

    w.mem.put<float>(kCamera + 0x160, 0.92f);
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FLOAT_EQ(s.cam_fov_y, 0.92f);

    for (float bad : {0.0f, -1.0f, 9.0f, std::numeric_limits<float>::quiet_NaN()}) {
        w.mem.put<float>(kCamera + 0x160, bad);
        ASSERT_EQ(binder.sample(s), SampleStatus::Ok); // an odd FOV never invalidates the whole sample
        EXPECT_FLOAT_EQ(s.cam_fov_y, 0.f) << "fov " << bad << " must be reported as unknown";
    }
}

TEST(SekiroLiveSampleTest, ReadsTheCharactersFacingDirectionFromItsModelTransform) {
    World w;
    for (float yaw : {0.f, 0.7f, 1.6f, -2.4f, 3.0f}) {
        w.setModelTransform(yaw, 10.f, -36.f, 5.f);
        LiveBinder binder(w.mem, kBase, kImageSize);
        ASSERT_EQ(binder.scan(), BindStatus::Bound);
        LiveSample s;
        ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
        ASSERT_TRUE(s.facing_valid) << "yaw " << yaw;
        EXPECT_NEAR(s.facing_x, std::sin(yaw), 1e-5f) << "yaw " << yaw;
        EXPECT_NEAR(s.facing_z, std::cos(yaw), 1e-5f) << "yaw " << yaw;
    }
}

TEST(SekiroLiveSampleTest, MissingOrImplausibleFacingNeverInvalidatesTheSample) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;

    ASSERT_EQ(binder.sample(s), SampleStatus::Ok); // no model object at all
    EXPECT_FALSE(s.facing_valid);

    // The matrix is only trusted when its translation is the player's position: that rejects any other
    // object that happens to look like a rotation.
    w.setModelTransform(0.5f, 10.f, -36.f, 5.f, /*break_translation=*/true);
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FALSE(s.facing_valid);

    w.setModelTransform(0.5f, 10.f, -36.f, 5.f);
    float skew = 0.4f; // not a rotation about Y any more
    w.mem.put(kChrModel + 0x34, skew);
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FALSE(s.facing_valid);

    w.setModelTransform(0.5f, 10.f, -36.f, 5.f);
    float scaled = 2.0f; // not unit length
    w.mem.put(kChrModel + 0x30, scaled);
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FALSE(s.facing_valid);

    w.setModelTransform(0.5f, 10.f, -36.f, 5.f);
    w.mem.put(kChrModel + 0x38, std::numeric_limits<float>::quiet_NaN());
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FALSE(s.facing_valid);
}

TEST(SekiroLiveSampleTest, ReadsThePlayersRealHealthFromTheDataModule) {
    World w;
    w.setVitals(1024, 1120);
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_TRUE(s.vitals_valid);
    EXPECT_FLOAT_EQ(s.hp, 1024.f);
    EXPECT_FLOAT_EQ(s.max_hp, 1120.f);

    w.setVitals(0, 1120); // dead: zero is a real reading, not "unknown"
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_TRUE(s.vitals_valid);
    EXPECT_FLOAT_EQ(s.hp, 0.f);
}

TEST(SekiroLiveSampleTest, ImplausibleOrUnidentifiedVitalsNeverInvalidateTheSample) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;

    ASSERT_EQ(binder.sample(s), SampleStatus::Ok); // no module container at all
    EXPECT_FALSE(s.vitals_valid);

    w.setVitals(500, 1120, /*right_class=*/false); // pointer chain leads to something else
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FALSE(s.vitals_valid);

    for (auto [hp, max_hp] : {std::pair<int32_t, int32_t>{500, 0}, {500, -5}, {-1, 1120}, {1121, 1120}, {500, 5000000}}) {
        w.setVitals(hp, max_hp);
        ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
        EXPECT_FALSE(s.vitals_valid) << hp << "/" << max_hp;
    }
}

TEST(SekiroLiveSampleTest, NotInWorldWhileManagersAreNull) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;

    w.mem.put<uint64_t>(kBase + kWcmGlobalRva, 0); // title screen
    EXPECT_EQ(binder.sample(s), SampleStatus::NotInWorld);

    w.mem.put<uint64_t>(kBase + kWcmGlobalRva, kWorld);
    w.mem.put<uint64_t>(kWorld + 0x88, 0); // loading: no player yet
    EXPECT_EQ(binder.sample(s), SampleStatus::NotInWorld);

    w.mem.put<uint64_t>(kWorld + 0x88, kPlayer);
    EXPECT_EQ(binder.sample(s), SampleStatus::Ok);
}

TEST(SekiroLiveSampleTest, AllZeroPositionMeansThePlayerIsNotPlacedYet) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;

    // Observed in-game: on world load the object exists for a frame before its position is written.
    w.setPlayer(0.f, 0.f, 0.f);
    EXPECT_EQ(binder.sample(s), SampleStatus::NotInWorld);

    w.setPlayer(0.f, -36.f, 0.f); // a single zero coordinate is perfectly normal
    EXPECT_EQ(binder.sample(s), SampleStatus::Ok);
}

TEST(SekiroLiveSampleTest, InvalidWhenPositionCopiesDisagreeOrAreNotFinite) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;

    w.setPlayer(10.f, -36.f, 5.f, /*copy_dx=*/5.f); // wrong offset / stale object
    EXPECT_EQ(binder.sample(s), SampleStatus::Invalid);

    w.setPlayer(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
    EXPECT_EQ(binder.sample(s), SampleStatus::Invalid);

    w.setPlayer(1e9f, 0.f, 0.f);
    EXPECT_EQ(binder.sample(s), SampleStatus::Invalid);

    w.setPlayer(10.f, -36.f, 5.f, 0.05f); // tiny frame skew between copies is fine
    EXPECT_EQ(binder.sample(s), SampleStatus::Ok);
}

TEST(SekiroLiveSampleTest, PicksTheCandidateWhoseCameraMatrixIsStructurallyValid) {
    World w;
    writeCamCode(w.mem, kCamDecoyPatternRva, kCamDecoyGlobalRva);
    w.mem.put<uint64_t>(kBase + kCamDecoyGlobalRva, kCameraDecoy);
    // decoy object: garbage where the matrix would be
    Mat garbage{{{3, 1, 4, 1}, {5, 9, 2, 6}, {5, 3, 5, 8}, {9, 7, 9, 3}}};
    w.mem.put(kCameraDecoy + 0xea0, garbage);

    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    EXPECT_EQ(binder.cameraCandidateRvas().size(), 2u);

    LiveSample s;
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FLOAT_EQ(s.cam_pos.X, 8.f);
}

TEST(SekiroLiveSampleTest, InvalidWhenNoCameraMatrixIsOrthonormal) {
    World w;
    Mat skewed = identityCamera(0, 0, 0);
    skewed.rows[2][0] = 0.5f; // forward no longer unit / perpendicular
    w.setCamera(skewed);
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;
    EXPECT_EQ(binder.sample(s), SampleStatus::Invalid);

    Mat bad_w = identityCamera(0, 0, 0);
    bad_w.rows[3][3] = 0.f; // a direction, not a position row
    w.setCamera(bad_w);
    EXPECT_EQ(binder.sample(s), SampleStatus::Invalid);
}

// ----------------------------------------------------------------------------- mirror

TEST(SekiroLiveMirrorTest, FillsMirrorAndDifferentiatesVelocity) {
    ChrIns player;
    ChrCam camera;
    LiveMirror mirror;
    LiveSample a;
    a.player_pos = {10.f, -36.f, 5.f};
    a.cam_pos = {8.f, -34.f, 2.f};
    a.cam_forward = {0.f, 0.f, 1.f};

    mirror.update(a, 0.016f, player, camera);
    EXPECT_FLOAT_EQ(player.Position.X, 10.f);
    EXPECT_FLOAT_EQ(player.Velocity.Length(), 0.f); // no previous sample yet
    EXPECT_FLOAT_EQ(camera.Position.Y, -34.f);
    EXPECT_FLOAT_EQ(camera.Forward.Z, 1.f);

    LiveSample b = a;
    b.player_pos = {10.f, -36.f, 5.f + 0.1f}; // 0.1 m in 20 ms = 5 m/s forward
    mirror.update(b, 0.02f, player, camera);
    EXPECT_NEAR(player.Velocity.Z, 5.f, 1e-3f);
    EXPECT_NEAR(player.Velocity.X, 0.f, 1e-4f);
}

TEST(SekiroLiveMirrorTest, CarriesTheFacingIntoTheMirrorOnlyWhileItIsValid) {
    ChrIns player;
    ChrCam camera;
    LiveMirror mirror;
    LiveSample s;
    s.player_pos = {1.f, 2.f, 3.f};
    s.facing_valid = true;
    s.facing_x = 0.6f;
    s.facing_z = 0.8f;
    mirror.update(s, 0.016f, player, camera);
    EXPECT_TRUE(player.bFacingValid);
    EXPECT_FLOAT_EQ(player.Facing.X, 0.6f);
    EXPECT_FLOAT_EQ(player.Facing.Z, 0.8f);

    s.facing_valid = false;
    mirror.update(s, 0.016f, player, camera);
    EXPECT_FALSE(player.bFacingValid);
}

TEST(SekiroLiveMirrorTest, CarriesVitalsIntoTheMirrorOnlyWhileValid) {
    ChrIns player;
    ChrCam camera;
    LiveMirror mirror;
    LiveSample s;
    s.player_pos = {1.f, 2.f, 3.f};
    s.vitals_valid = true;
    s.hp = 700.f;
    s.max_hp = 1120.f;
    mirror.update(s, 0.016f, player, camera);
    EXPECT_TRUE(player.bVitalsValid);
    EXPECT_FLOAT_EQ(player.Health, 700.f);
    EXPECT_FLOAT_EQ(player.MaxHealth, 1120.f);

    s.vitals_valid = false;
    s.hp = 0.f;
    mirror.update(s, 0.016f, player, camera);
    EXPECT_FALSE(player.bVitalsValid);
    EXPECT_FLOAT_EQ(player.Health, 700.f); // last good value kept, flagged unusable
}

TEST(SekiroLiveMirrorTest, TeleportAndResetDoNotProduceVelocitySpikes) {
    ChrIns player;
    ChrCam camera;
    LiveMirror mirror;
    LiveSample a;
    a.player_pos = {0.f, 0.f, 0.f};
    mirror.update(a, 0.016f, player, camera);

    LiveSample far = a;
    far.player_pos = {500.f, 0.f, 0.f}; // fast travel / respawn
    mirror.update(far, 0.016f, player, camera);
    EXPECT_FLOAT_EQ(player.Velocity.Length(), 0.f);

    mirror.reset();
    LiveSample next = far;
    next.player_pos = {501.f, 0.f, 0.f};
    mirror.update(next, 0.016f, player, camera);
    EXPECT_FLOAT_EQ(player.Velocity.Length(), 0.f); // first sample after reset

    mirror.update(next, 0.0f, player, camera); // zero dt must not divide by zero
    EXPECT_TRUE(std::isfinite(player.Velocity.X));
}

TEST(SekiroLiveMirrorTest, LeavesAdapterOwnedModelFlagsAlone) {
    ChrIns player;
    ChrCam camera;
    player.bModelHidden = true;
    player.ModelAlpha = 0.f;
    LiveMirror mirror;
    LiveSample a;
    mirror.update(a, 0.016f, player, camera);
    EXPECT_TRUE(player.bModelHidden);
    EXPECT_FLOAT_EQ(player.ModelAlpha, 0.f);
}

// ---------------------------------------------------------------------------------------------
// The game's camera object only changes every other frame (30 Hz) while the player moves every frame.
// Projecting with the stale camera makes the rig bounce on screen, so the camera is carried along with the
// player on the frames where it did not update.
// ---------------------------------------------------------------------------------------------
namespace {
LiveSample frameSample(int frame, bool camera_updates_this_frame, float speed_m_per_frame = 0.06f) {
    LiveSample s;
    s.player_pos = {0.f, 0.f, speed_m_per_frame * static_cast<float>(frame)};
    const int camera_frame = camera_updates_this_frame ? frame : frame - (frame % 2);
    s.cam_pos = {0.f, 1.5f, speed_m_per_frame * static_cast<float>(camera_frame) - 4.f};
    s.cam_forward = {0.f, 0.f, 1.f};
    return s;
}
float cameraToPlayerDistance(const LiveSample& s) {
    return s.player_pos.Z - s.cam_pos.Z;
}
} // namespace

TEST(SekiroCameraStabilizerTest, KeepsTheCameraRigidlyBehindThePlayerWhenTheCameraOnlyUpdatesEveryOtherFrame) {
    CameraStabilizer stabilizer;
    float lo = 1e9f, hi = -1e9f;
    for (int frame = 0; frame < 60; ++frame) {
        LiveSample s = frameSample(frame, frame % 2 == 0);
        stabilizer.apply(s);
        if (frame >= 2) {
            lo = std::min(lo, cameraToPlayerDistance(s));
            hi = std::max(hi, cameraToPlayerDistance(s));
        }
    }
    EXPECT_NEAR(hi - lo, 0.f, 1e-4f) << "raw data wobbles by one frame of motion (0.06 m)";
}

TEST(SekiroCameraStabilizerTest, LeavesACameraThatUpdatesEveryFrameAlone) {
    CameraStabilizer stabilizer;
    for (int frame = 0; frame < 20; ++frame) {
        LiveSample s = frameSample(frame, true);
        const LiveSample raw = s;
        stabilizer.apply(s);
        EXPECT_FLOAT_EQ(s.cam_pos.Z, raw.cam_pos.Z);
        EXPECT_FLOAT_EQ(s.cam_pos.Y, raw.cam_pos.Y);
    }
}

TEST(SekiroCameraStabilizerTest, FirstSampleAndStationaryPlayerPassThroughUnchanged) {
    CameraStabilizer stabilizer;
    LiveSample a = frameSample(0, true, 0.f);
    const LiveSample raw = a;
    stabilizer.apply(a);
    EXPECT_FLOAT_EQ(a.cam_pos.Z, raw.cam_pos.Z);
    for (int i = 0; i < 10; ++i) {
        LiveSample s = frameSample(0, true, 0.f); // nothing moves, the camera repeats forever
        stabilizer.apply(s);
        EXPECT_FLOAT_EQ(s.cam_pos.Z, raw.cam_pos.Z);
    }
}

TEST(SekiroCameraStabilizerTest, DoesNotDragAFrozenCameraAlongForever) {
    // The camera stops following (cutscene, lock): after a couple of repeats the raw value must win again.
    CameraStabilizer stabilizer;
    LiveSample first = frameSample(0, true);
    stabilizer.apply(first);
    LiveSample s;
    for (int frame = 1; frame <= 8; ++frame) {
        s = frameSample(frame, true);
        s.cam_pos = first.cam_pos; // identical camera every frame while the player walks away
        stabilizer.apply(s);
    }
    EXPECT_NEAR(s.cam_pos.Z, first.cam_pos.Z, 1e-5f);
}

TEST(SekiroCameraStabilizerTest, TeleportResetsTheOffset) {
    CameraStabilizer stabilizer;
    LiveSample a = frameSample(0, true);
    stabilizer.apply(a);
    LiveSample b = frameSample(1, false);
    b.player_pos = {500.f, 0.f, 500.f}; // loaded another area; camera not updated yet
    b.cam_pos = a.cam_pos;
    stabilizer.apply(b);
    EXPECT_NEAR(b.cam_pos.Z, a.cam_pos.Z, 1e-5f); // not shifted by 700 m
}

TEST(SekiroCameraStabilizerTest, ResetForgetsThePreviousFrame) {
    CameraStabilizer stabilizer;
    LiveSample a = frameSample(0, true);
    stabilizer.apply(a);
    stabilizer.reset();
    LiveSample b = frameSample(5, false); // stale camera, but there is nothing to extrapolate from
    const LiveSample raw = b;
    stabilizer.apply(b);
    EXPECT_FLOAT_EQ(b.cam_pos.Z, raw.cam_pos.Z);
}


// ---------------------------------------------------------------------------------------------
// Enemy enumeration: WorldChrMan+0xC8 -> WorldBlockChr, a slot array of EnemyIns (verified in the live game)
// ---------------------------------------------------------------------------------------------
TEST(SekiroLiveEnemiesTest, ListsActiveEnemiesWithTheirRealHealthAndPosition) {
    World w;
    const uintptr_t first = w.addEnemy(4, {.x = 164.f, .y = -29.f, .z = 23.f, .char_id = 10010000, .hp = 1500, .max_hp = 2101});
    w.addEnemy(7, {.x = 10.f, .y = 2.f, .z = 3.f, .char_id = 10100300, .hp = 200, .max_hp = 322});
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);

    std::vector<LiveEnemy> enemies;
    ASSERT_EQ(binder.enumerateEnemies(enemies), 2u);
    EXPECT_EQ(enemies[0].slot, 4u);
    EXPECT_EQ(enemies[0].handle, first);
    EXPECT_EQ(enemies[0].char_id, 10010000u);
    EXPECT_EQ(enemies[0].team, 5u);
    EXPECT_TRUE(enemies[0].hostile);
    EXPECT_FLOAT_EQ(enemies[0].position.X, 164.f);
    EXPECT_FLOAT_EQ(enemies[0].position.Y, -29.f);
    EXPECT_TRUE(enemies[0].hp_valid);
    EXPECT_FLOAT_EQ(enemies[0].hp, 1500.f);
    EXPECT_FLOAT_EQ(enemies[0].max_hp, 2101.f);
    EXPECT_FALSE(enemies[0].dead);
    EXPECT_EQ(enemies[1].slot, 7u);
}

TEST(SekiroLiveEnemiesTest, UsesTheLivePositionNotTheStaticSpawnPosition) {
    World w;
    w.addEnemy(1, {.x = 100.f, .y = 5.f, .z = 100.f, .live_y = 6.5f}); // moved: live differs from the spawn copy
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    std::vector<LiveEnemy> enemies;
    ASSERT_EQ(binder.enumerateEnemies(enemies), 1u);
    EXPECT_FLOAT_EQ(enemies[0].position.Y, 6.5f);
}

TEST(SekiroLiveEnemiesTest, SkipsInactiveEmptyAndWrongClassSlots) {
    World w;
    w.addEnemy(0, {.x = 50.f, .y = 1.f, .z = 50.f, .active = false});      // unloaded: no live position
    w.addEnemy(2, {.x = 1.f, .y = 1.f, .z = 1.f, .right_class = false});   // not an EnemyIns
    w.addEnemy(3, {.x = 2.f, .y = 1.f, .z = 2.f});                         // the only real one
    // slot 1 stays a null pointer
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    std::vector<LiveEnemy> enemies;
    ASSERT_EQ(binder.enumerateEnemies(enemies), 1u);
    EXPECT_EQ(enemies[0].slot, 3u);
}

TEST(SekiroLiveEnemiesTest, ClassifiesTeamsAndDeathFromHealth) {
    World w;
    w.addEnemy(0, {.x = 1.f, .y = 1.f, .z = 1.f, .team = 5});
    w.addEnemy(1, {.x = 2.f, .y = 1.f, .z = 2.f, .team = 9});              // neutral
    w.addEnemy(2, {.x = 3.f, .y = 1.f, .z = 3.f, .team = 1});              // friendly
    w.addEnemy(3, {.x = 4.f, .y = 1.f, .z = 4.f, .hp = 0});                // dead
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    std::vector<LiveEnemy> e;
    ASSERT_EQ(binder.enumerateEnemies(e), 4u);
    EXPECT_TRUE(e[0].hostile);
    EXPECT_FALSE(e[1].hostile);
    EXPECT_FALSE(e[2].hostile);
    EXPECT_TRUE(e[3].dead);
    EXPECT_TRUE(e[3].hp_valid);
}

TEST(SekiroLiveEnemiesTest, UntrustworthyHealthIsReportedAsUnknownNotAsZero) {
    World w;
    w.addEnemy(0, {.x = 1.f, .y = 1.f, .z = 1.f, .module_ok = false});          // module class mismatch (garbage)
    w.addEnemy(1, {.x = 2.f, .y = 1.f, .z = 2.f, .hp = 500, .max_hp = 0});      // impossible max
    w.addEnemy(2, {.x = 3.f, .y = 1.f, .z = 3.f, .hp = 9000, .max_hp = 100});   // hp above max
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    std::vector<LiveEnemy> e;
    ASSERT_EQ(binder.enumerateEnemies(e), 3u); // still listed: position is real
    for (const auto& enemy : e) {
        EXPECT_FALSE(enemy.hp_valid);
        EXPECT_FALSE(enemy.dead) << "unknown health must not look like a corpse";
    }
}

TEST(SekiroLiveEnemiesTest, NothingWithoutABlockAndABoundedWalkWithAHugeSlotCount) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    std::vector<LiveEnemy> e;
    EXPECT_EQ(binder.enumerateEnemies(e), 0u); // no block pointer

    w.addEnemy(5, {.x = 1.f, .y = 1.f, .z = 1.f});
    w.ensureBlock(1000000); // corrupt count: must not read a million slots
    EXPECT_EQ(binder.enumerateEnemies(e), 0u);

    w.ensureBlock(144);
    w.mem.put<uint64_t>(kBlock, kBase + 0x7777); // wrong class for the block
    EXPECT_EQ(binder.enumerateEnemies(e), 0u);
}

TEST(SekiroLiveEnemiesTest, HonoursTheMaxEntriesLimit) {
    World w;
    for (int i = 0; i < 6; ++i) w.addEnemy(i, {.x = float(i + 1), .y = 1.f, .z = 1.f});
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    std::vector<LiveEnemy> e;
    EXPECT_EQ(binder.enumerateEnemies(e, 4), 4u);
}


// ---------------------------------------------------------------------------------------------
// Real grounded flag: SprjPlayerFallModule + 0x40 is -1 on the ground
// ---------------------------------------------------------------------------------------------
TEST(SekiroLiveGroundedTest, MinusOneMeansGroundedAndNonNegativeMeansAirborne) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;

    w.setFallState(-1);
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_TRUE(s.grounded_valid);
    EXPECT_TRUE(s.grounded);

    for (int32_t airborne : {0, 1, 3, 12}) {
        w.setFallState(airborne);
        ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
        EXPECT_TRUE(s.grounded_valid);
        EXPECT_FALSE(s.grounded) << airborne;
    }
}

TEST(SekiroLiveGroundedTest, MissingOrWrongClassModuleMeansUnknownNotAirborne) {
    World w;
    LiveBinder binder(w.mem, kBase, kImageSize);
    ASSERT_EQ(binder.scan(), BindStatus::Bound);
    LiveSample s;
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FALSE(s.grounded_valid);

    w.setFallState(5, /*right_class=*/false);
    ASSERT_EQ(binder.sample(s), SampleStatus::Ok);
    EXPECT_FALSE(s.grounded_valid);
    EXPECT_TRUE(s.grounded) << "unknown defaults to grounded so nothing starts gliding by accident";
}

TEST(SekiroLiveMirrorTest, CarriesTheGroundedFlagOnlyWhileValid) {
    ChrIns player;
    ChrCam camera;
    LiveMirror mirror;
    LiveSample s;
    s.player_pos = {1.f, 2.f, 3.f};
    s.grounded_valid = true;
    s.grounded = false;
    mirror.update(s, 0.016f, player, camera);
    EXPECT_TRUE(player.bGroundedValid);
    EXPECT_FALSE(player.bOnGround);
    s.grounded_valid = false;
    mirror.update(s, 0.016f, player, camera);
    EXPECT_FALSE(player.bGroundedValid);
}
