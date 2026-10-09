#include <gtest/gtest.h>

#include "eldenring_camera.hpp"
#include "eldenring_rtti.hpp"
#include "eldenring_singletons.hpp"
#include "eldenring_state.hpp"
#include "eldenring_world.hpp"

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace eldenring::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;

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
    void putString(uintptr_t address, const std::string& s) {
        for (size_t i = 0; i <= s.size(); ++i) put<char>(address + i, i < s.size() ? s[i] : '\0');
    }
};

// A class whose vtable lives at `vtable_rva` with RTTI name `name`: COL at col_rva, TypeDescriptor at td_rva.
void addRttiClass(Mem& m, uint32_t vtable_rva, uint32_t col_rva, uint32_t td_rva, const std::string& name) {
    m.region(kBase + vtable_rva - 8, 16);
    m.region(kBase + col_rva, 0x20);
    m.region(kBase + td_rva, 0x10 + name.size() + 1);
    m.put<uint64_t>(kBase + vtable_rva - 8, kBase + col_rva);
    m.put<uint32_t>(kBase + col_rva, 1);
    m.put<uint32_t>(kBase + col_rva + 0xC, td_rva);
    m.putString(kBase + td_rva + 0x10, name);
}

} // namespace

// ---- RTTI ------------------------------------------------------------------------------------------------

TEST(EldenRingRtti, ReadsTheMangledNameBehindAVtable) {
    Mem m;
    addRttiClass(m, 0x2A3C890, 0x32D4270, 0x2A3C370, ".?AVCSChrPhysicsModule@CS@@");
    const auto name = readTypeNameOfVtable(m, kBase, kBase + 0x2A3C890);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, ".?AVCSChrPhysicsModule@CS@@");
}

TEST(EldenRingRtti, RejectsWrongSignatureNullAndUnreadable) {
    Mem m;
    addRttiClass(m, 0x1000, 0x2000, 0x3000, ".?AVX@@");
    m.put<uint32_t>(kBase + 0x2000, 0); // x86-style / bogus COL signature
    EXPECT_FALSE(readTypeNameOfVtable(m, kBase, kBase + 0x1000).has_value());
    EXPECT_FALSE(readTypeNameOfVtable(m, kBase, 0).has_value());
    EXPECT_FALSE(readTypeNameOfVtable(m, kBase, 0x7ff000000000ull).has_value());
}

TEST(EldenRingRtti, UnterminatedNameIsRejected) {
    Mem m;
    addRttiClass(m, 0x1000, 0x2000, 0x3000, "A");
    m.regions[kBase + 0x3000].assign(0x10 + rtti::kMaxNameLength + 8, 'A');
    EXPECT_FALSE(readTypeNameOfVtable(m, kBase, kBase + 0x1000).has_value());
}

TEST(EldenRingRtti, ObjectIsClassAcceptsMeasuredVtableOrRttiName) {
    Mem m;
    addRttiClass(m, 0x2A3A0D8, 0x32D2F18, 0x2A39500, ".?AVCSEnemyDamageModule@CS@@");
    m.region(0x7ff500000000ull, 0x10);
    m.put<uint64_t>(0x7ff500000000ull, kBase + 0x2A3A0D8);
    // Measured vtable matches.
    EXPECT_TRUE(objectIsClass(m, kBase, 0x7ff500000000ull, 0x2A3A0D8, "Nope"));
    // After a patch the RVA moved; the RTTI name still identifies the class.
    EXPECT_TRUE(objectIsClass(m, kBase, 0x7ff500000000ull, 0x1234, "DamageModule"));
    EXPECT_FALSE(objectIsClass(m, kBase, 0x7ff500000000ull, 0x1234, "PhysicsModule"));
    EXPECT_FALSE(objectIsClass(m, kBase, 0, 0x2A3A0D8, "DamageModule"));
}

// ---- singleton signatures --------------------------------------------------------------------------------

namespace {
// Builds an image where `pattern_bytes` (disp32 filled in) sits at `at`, with the global variable at `global_rva`.
std::vector<uint8_t> imageWith(size_t size, size_t at, const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> img(size, 0xCC);
    std::copy(bytes.begin(), bytes.end(), img.begin() + at);
    return img;
}
void putDisp(std::vector<uint8_t>& img, size_t at, int32_t d) { std::memcpy(img.data() + at, &d, 4); }
} // namespace

TEST(EldenRingSingleton, ResolvesTheCsFadeGlobalFromItsStoreInstruction) {
    // 48 8B F8 | 48 89 3D disp32 | 48 8B C6  -> insn at +3, len 7, disp at +6.
    std::vector<uint8_t> bytes = {0x48, 0x8B, 0xF8, 0x48, 0x89, 0x3D, 0, 0, 0, 0, 0x48, 0x8B, 0xC6};
    auto img = imageWith(0x2000, 0x100, bytes);
    const size_t global = 0x1800;
    putDisp(img, 0x100 + 6, static_cast<int32_t>(global - (0x100 + 3 + 7)));
    const auto rva = locateGlobalRva(img.data(), img.size(), sigs::kCSFade);
    ASSERT_TRUE(rva.has_value());
    EXPECT_EQ(*rva, global);
}

TEST(EldenRingSingleton, ResolvesWorldChrManWithTheLongerOffsets) {
    std::vector<uint8_t> bytes = {0x41, 0xFF, 0x10, 0x4C, 0x8B, 0x03, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCB, 0x41, 0xFF, 0x50, 0x68,
                                  0x48, 0x89, 0x3D, 0, 0, 0, 0, 0x48, 0x8B, 0x35, 0, 0, 0, 0};
    auto img = imageWith(0x4000, 0x200, bytes);
    const size_t global = 0x3000;
    putDisp(img, 0x200 + 19, static_cast<int32_t>(global - (0x200 + 16 + 7)));
    const auto rva = locateGlobalRva(img.data(), img.size(), sigs::kWorldChrMan);
    ASSERT_TRUE(rva.has_value());
    EXPECT_EQ(*rva, global);
}

TEST(EldenRingSingleton, ScanningOnlyTheCodeSectionStillResolvesGlobalsInOtherSections) {
    // The code section starts at RVA 0x1000 and is 0x1000 long; the image is 0x5000 and the global lives at 0x4800.
    std::vector<uint8_t> bytes = {0x48, 0x8B, 0xF8, 0x48, 0x89, 0x3D, 0, 0, 0, 0, 0x48, 0x8B, 0xC6};
    auto code = imageWith(0x1000, 0x80, bytes);
    const uint32_t code_rva = 0x1000, image_size = 0x5000, global = 0x4800;
    putDisp(code, 0x80 + 6, static_cast<int32_t>(global - (code_rva + 0x80 + 3 + 7)));
    const auto rva = locateGlobalRva(code.data(), code.size(), code_rva, image_size, sigs::kCSFade);
    ASSERT_TRUE(rva.has_value());
    EXPECT_EQ(*rva, global);
    // The same buffer treated as the whole image would reject the target (it is past the buffer).
    EXPECT_FALSE(locateGlobalRva(code.data(), code.size(), sigs::kCSFade).has_value());
    // A target beyond SizeOfImage is refused.
    EXPECT_FALSE(locateGlobalRva(code.data(), code.size(), code_rva, 0x2000, sigs::kCSFade).has_value());
}

TEST(EldenRingSingleton, NegativeDisplacementWorks) {
    std::vector<uint8_t> bytes = {0x48, 0x8B, 0xF8, 0x48, 0x89, 0x3D, 0, 0, 0, 0, 0x48, 0x8B, 0xC6};
    auto img = imageWith(0x2000, 0x1000, bytes);
    putDisp(img, 0x1000 + 6, static_cast<int32_t>(0x200) - static_cast<int32_t>(0x1000 + 3 + 7));
    const auto rva = locateGlobalRva(img.data(), img.size(), sigs::kCSFade);
    ASSERT_TRUE(rva.has_value());
    EXPECT_EQ(*rva, 0x200u);
}

TEST(EldenRingSingleton, TwoMatchesOrNoMatchOrOutOfImageFailClosed) {
    std::vector<uint8_t> bytes = {0x48, 0x8B, 0xF8, 0x48, 0x89, 0x3D, 0, 0, 0, 0, 0x48, 0x8B, 0xC6};
    auto img = imageWith(0x2000, 0x100, bytes);
    std::copy(bytes.begin(), bytes.end(), img.begin() + 0x400); // second match
    EXPECT_FALSE(locateGlobalRva(img.data(), img.size(), sigs::kCSFade).has_value());
    const std::vector<uint8_t> empty(0x100, 0xCC);
    EXPECT_FALSE(locateGlobalRva(empty.data(), empty.size(), sigs::kCSFade).has_value());
    auto far = imageWith(0x2000, 0x100, bytes);
    putDisp(far, 0x100 + 6, 0x7fffffff); // target far outside the image
    EXPECT_FALSE(locateGlobalRva(far.data(), far.size(), sigs::kCSFade).has_value());
}

TEST(EldenRingSingleton, ReadSingletonRequiresTheExpectedRttiType) {
    Mem m;
    addRttiClass(m, 0x2A5000, 0x32E000, 0x2A4000, ".?AVCSFadeImp@CS@@");
    addRttiClass(m, 0x2A6000, 0x32E100, 0x2A4100, ".?AVSomethingElse@@");
    addRttiClass(m, 0x2A7000, 0x32E200, 0x2A4200, ".?AVCSFadePlateImp@CS@@");
    m.region(kBase + 0x3D763B0, 8);
    m.region(0x7ff300000000ull, 0x10);
    m.put<uint64_t>(kBase + 0x3D763B0, 0x7ff300000000ull);
    m.put<uint64_t>(0x7ff300000000ull, kBase + 0x2A5000);
    EXPECT_EQ(readSingleton(m, kBase, 0x3D763B0, sigs::kCSFade), 0x7ff300000000ull);
    // The signature found a different global: the object has another type, so it is refused.
    m.put<uint64_t>(0x7ff300000000ull, kBase + 0x2A6000);
    EXPECT_EQ(readSingleton(m, kBase, 0x3D763B0, sigs::kCSFade), 0u);
    // A lookalike class that merely contains the name is refused as well (exact RTTI name is required).
    m.put<uint64_t>(0x7ff300000000ull, kBase + 0x2A7000);
    EXPECT_EQ(readSingleton(m, kBase, 0x3D763B0, sigs::kCSFade), 0u);
    m.put<uint64_t>(kBase + 0x3D763B0, 0); // not created yet
    EXPECT_EQ(readSingleton(m, kBase, 0x3D763B0, sigs::kCSFade), 0u);
}

// ---- UI state --------------------------------------------------------------------------------------------

TEST(EldenRingState, MenuLayersAndPopup) {
    Mem m;
    const uintptr_t menu = 0x7ff400000000ull;
    m.region(menu, 0x900);
    MenuState s;
    m.put<uint8_t>(menu + layout::kMenuDisableMouseCursor, 1); // open world, cursor disabled
    ASSERT_TRUE(readMenuState(m, menu, s));
    EXPECT_FALSE(s.menu_focused);
    EXPECT_FALSE(s.popup_open);
    // Main menu hub: +0x1C = 0x10. The cursor flag does NOT move with menus (live capture), so it is not used.
    m.put<uint8_t>(menu + layout::kMenuLayerMaskA, 0x10);
    ASSERT_TRUE(readMenuState(m, menu, s));
    EXPECT_TRUE(s.menu_focused);
    EXPECT_FALSE(s.cursor_released);
    // System menu: +0x1D = 1.
    m.put<uint8_t>(menu + layout::kMenuLayerMaskA, 0);
    m.put<uint8_t>(menu + layout::kMenuLayerMaskB, 1);
    ASSERT_TRUE(readMenuState(m, menu, s));
    EXPECT_TRUE(s.menu_focused);
    EXPECT_EQ(s.layer_mask_b, 1);
    // Popup pointer set while the cursor is disabled during a load.
    m.put<uint8_t>(menu + layout::kMenuLayerMaskB, 0);
    m.put<uint8_t>(menu + layout::kMenuDisableMouseCursor, 0);
    m.put<uint64_t>(menu + layout::kMenuPopup, 0x7ff2a13488c0ull);
    ASSERT_TRUE(readMenuState(m, menu, s));
    EXPECT_FALSE(s.menu_focused);
    EXPECT_TRUE(s.cursor_released);
    EXPECT_TRUE(s.popup_open);
    EXPECT_FALSE(readMenuState(m, 0, s));
    EXPECT_FALSE(readMenuState(m, 0x7ff500000000ull, s));
}

TEST(EldenRingState, UiStatesAreExposedRaw) {
    Mem m;
    const uintptr_t menu = 0x7ff400000000ull;
    m.region(menu, 0x900);
    m.put<uint8_t>(menu + layout::kMenuUiStates + 0x3D, 7);
    uint8_t states[layout::kMenuUiStateCount] = {};
    ASSERT_TRUE(readUiStates(m, menu, states));
    EXPECT_EQ(states[0x3D], 7);
    EXPECT_EQ(states[0], 0);
}

TEST(EldenRingState, LoadingScreenIsTheLatchNotTheJobPointer) {
    Mem m;
    const uintptr_t helper = 0x7ff3a2ac1d00ull;
    m.region(helper, 0x100);
    m.put<int32_t>(helper + layout::kLoadCounter, 3);
    m.put<uint8_t>(helper + layout::kLoadingLatch, 1); // playing
    LoadingState s;
    ASSERT_TRUE(readLoadingState(m, helper, s));
    EXPECT_FALSE(s.job_active);
    EXPECT_FALSE(s.screen_loading);
    EXPECT_EQ(s.load_counter, 3);
    // Teleport starts: the job exists and the latch drops.
    m.put<uint64_t>(helper + layout::kLoadingJob, 0x7ff2a35e2260ull);
    m.put<uint8_t>(helper + layout::kLoadingLatch, 0);
    m.put<int32_t>(helper + layout::kLoadCounter, 4);
    ASSERT_TRUE(readLoadingState(m, helper, s));
    EXPECT_TRUE(s.job_active);
    EXPECT_TRUE(s.screen_loading);
    EXPECT_EQ(s.load_counter, 4);
    // About one second later the job is gone but the black loading screen goes on.
    m.put<uint64_t>(helper + layout::kLoadingJob, 0);
    ASSERT_TRUE(readLoadingState(m, helper, s));
    EXPECT_FALSE(s.job_active);
    EXPECT_TRUE(s.screen_loading);
    EXPECT_FALSE(readLoadingState(m, 0, s));
}

namespace {
Mem makeFade(float screen_alpha) {
    Mem m;
    const uintptr_t fade = 0x7ff3a27149c0ull;
    m.region(fade, 0x100);
    for (uint32_t i = 0; i < layout::kFadePlateCount; ++i) {
        const uintptr_t plate = 0x7ff3a2a01800ull + i * 0xC0;
        m.region(plate, 0xC0);
        m.put<uint64_t>(fade + layout::kFadePlates + i * 8, plate);
    }
    m.put<float>(0x7ff3a2a01800ull + layout::kFadeScreenPlate * 0xC0 + layout::kFadePlateAlpha, screen_alpha);
    return m;
}
constexpr uintptr_t kFadeObj = 0x7ff3a27149c0ull;
} // namespace

TEST(EldenRingState, FadeAlphaFollowsTheScreenPlateOnly) {
    Mem m = makeFade(0.6f);
    // Other plates (7, 8, 3) never change the answer, whatever they hold.
    for (uint32_t i : {3u, 7u, 8u}) m.put<float>(0x7ff3a2a01800ull + i * 0xC0 + layout::kFadePlateAlpha, 1.0f);
    float a = -1.f;
    ASSERT_TRUE(readFadeAlpha(m, kFadeObj, a));
    EXPECT_FLOAT_EQ(a, 0.6f);
    m.put<float>(0x7ff3a2a01800ull + layout::kFadeScreenPlate * 0xC0 + layout::kFadePlateAlpha, 0.f);
    ASSERT_TRUE(readFadeAlpha(m, kFadeObj, a));
    EXPECT_FLOAT_EQ(a, 0.f);
}

TEST(EldenRingState, FadeAlphaRejectsGarbage) {
    Mem m = makeFade(10.0f); // the +0x5C rate, not an alpha
    float a = 5.f;
    EXPECT_FALSE(readFadeAlpha(m, kFadeObj, a));
    m.put<float>(0x7ff3a2a01800ull + layout::kFadeScreenPlate * 0xC0 + layout::kFadePlateAlpha, std::nanf(""));
    EXPECT_FALSE(readFadeAlpha(m, kFadeObj, a));
    EXPECT_FLOAT_EQ(a, 5.f);
    m.put<uint64_t>(kFadeObj + layout::kFadePlates + layout::kFadeScreenPlate * 8, 0); // plate missing
    EXPECT_FALSE(readFadeAlpha(m, kFadeObj, a));
    EXPECT_FALSE(readFadeAlpha(m, 0, a));
}

// ---- camera ----------------------------------------------------------------------------------------------

namespace {
Mem makeCamera(float fov, const float pos[3]) {
    Mem m;
    const uintptr_t world = 0x7ff500000000ull, cam = 0x7ff500100000ull;
    m.region(world, 0x20000);
    m.region(cam, 0x100);
    m.put<uint64_t>(world + layout::kChrCamInWorldChrMan, cam);
    m.put<uint64_t>(cam, kBase + layout::kChrCamVtableRva);
    const float mat[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, pos[0], pos[1], pos[2], 1}; // looking along +Z
    for (int i = 0; i < 16; ++i) m.put<float>(cam + layout::kCamMatrix + i * 4, mat[i]);
    m.put<float>(cam + layout::kCamFovY, fov);
    return m;
}
} // namespace

TEST(EldenRingCamera, ReadsMatrixAndVerticalFov) {
    const float pos[3] = {1, 2, 3};
    Mem m = makeCamera(0.8378f, pos);
    CameraPose c;
    ASSERT_TRUE(readCamera(m, kBase, 0x7ff500000000ull, c));
    EXPECT_FLOAT_EQ(c.position[2], 3.f);
    EXPECT_FLOAT_EQ(c.forward[2], 1.f);
    EXPECT_FLOAT_EQ(c.fov_y, 0.8378f);
}

TEST(EldenRingCamera, RejectsWrongClassBadFovAndNonFinite) {
    const float pos[3] = {0, 0, 0};
    CameraPose c;
    Mem wrong = makeCamera(0.8f, pos);
    wrong.put<uint64_t>(0x7ff500100000ull, kBase + 0x1234);
    EXPECT_FALSE(readCamera(wrong, kBase, 0x7ff500000000ull, c));
    EXPECT_FALSE(readCamera(makeCamera(0.f, pos), kBase, 0x7ff500000000ull, c));
    EXPECT_FALSE(readCamera(makeCamera(3.5f, pos), kBase, 0x7ff500000000ull, c));
    Mem nan = makeCamera(0.8f, pos);
    nan.put<float>(0x7ff500100000ull + layout::kCamMatrix + 12 * 4, std::nanf(""));
    EXPECT_FALSE(readCamera(nan, kBase, 0x7ff500000000ull, c));
    EXPECT_FALSE(readCamera(makeCamera(0.8f, pos), kBase, 0, c));
}

TEST(EldenRingCamera, ProjectsStraightAheadToTheCentre) {
    CameraPose c;
    c.right[0] = 1; c.up[1] = 1; c.forward[2] = 1;
    c.fov_y = 0.8378f;
    const float p[3] = {0, 0, 10};
    const auto s = projectToScreen(c, p, 1920.f, 1080.f);
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(s->x, 960.f, 1e-3);
    EXPECT_NEAR(s->y, 540.f, 1e-3);
    EXPECT_FLOAT_EQ(s->depth, 10.f);
}

TEST(EldenRingCamera, RightIsRightAndUpIsUpOnScreen) {
    CameraPose c;
    c.right[0] = 1; c.up[1] = 1; c.forward[2] = 1;
    c.fov_y = 0.8378f;
    const float right[3] = {1, 0, 10}, up[3] = {0, 1, 10};
    EXPECT_GT(projectToScreen(c, right, 1920.f, 1080.f)->x, 960.f);
    EXPECT_LT(projectToScreen(c, up, 1920.f, 1080.f)->y, 540.f);
}

TEST(EldenRingCamera, VerticalFovPutsTheEdgeOnTheScreenBorder) {
    CameraPose c;
    c.right[0] = 1; c.up[1] = 1; c.forward[2] = 1;
    c.fov_y = 0.8378f;
    const float z = 7.f;
    const float top[3] = {0, z * std::tan(c.fov_y / 2.f), z};
    EXPECT_NEAR(projectToScreen(c, top, 1920.f, 1080.f)->y, 0.f, 1e-2);
    // The horizontal half extent grows with the aspect ratio (vertical FOV is fixed).
    const float side[3] = {z * std::tan(c.fov_y / 2.f) * (1920.f / 1080.f), 0, z};
    EXPECT_NEAR(projectToScreen(c, side, 1920.f, 1080.f)->x, 1920.f, 1e-2);
}

TEST(EldenRingCamera, BehindTheCameraAndInvalidSizesAreNotProjected) {
    CameraPose c;
    c.right[0] = 1; c.up[1] = 1; c.forward[2] = 1;
    c.fov_y = 0.8378f;
    const float behind[3] = {0, 0, -5}, near_plane[3] = {0, 0, 0.01f}, ok[3] = {0, 0, 5};
    EXPECT_FALSE(projectToScreen(c, behind, 1920.f, 1080.f).has_value());
    EXPECT_FALSE(projectToScreen(c, near_plane, 1920.f, 1080.f).has_value());
    EXPECT_FALSE(projectToScreen(c, ok, 0.f, 1080.f).has_value());
}

TEST(EldenRingCamera, CameraPositionIsAccountedFor) {
    CameraPose c;
    c.right[0] = 1; c.up[1] = 1; c.forward[2] = 1;
    c.position[0] = 100.f; c.position[1] = 5.f; c.position[2] = -20.f;
    c.fov_y = 0.8378f;
    const float p[3] = {100.f, 5.f, -10.f};
    const auto s = projectToScreen(c, p, 1920.f, 1080.f);
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(s->x, 960.f, 1e-3);
    EXPECT_NEAR(s->y, 540.f, 1e-3);
}
