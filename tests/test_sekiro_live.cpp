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
