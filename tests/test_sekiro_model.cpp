#include <gtest/gtest.h>

#include "sekiro_model.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace sekiro::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr size_t kImageSize = 0x4000;
constexpr uintptr_t kWorld = 0x7ff400000000ull;
constexpr uintptr_t kPlayer = 0x7ff400100000ull;
constexpr uintptr_t kModel = 0x7ff400200000ull;
constexpr uintptr_t kEntity = 0x7ff400300000ull;
constexpr uintptr_t kEntity2 = 0x7ff400400000ull;
constexpr uint32_t kWcmRva = 0x1000;

constexpr uint32_t kVtableRva = 0x2000;   // vtable lives in the image, RTTI col pointer at vtable-8
constexpr uint32_t kColRva = 0x2100;
constexpr uint32_t kTypeDescRva = 0x2200;

class FakeMemory : public IMemoryReader, public IMemoryWriter {
public:
    std::map<uintptr_t, std::vector<uint8_t>> regions;
    int writes{0};
    bool fail_writes{false};
    int fail_on_write_number{-1}; // 1-based: that single write fails, the others succeed
    int attempts{0};

    bool read(uintptr_t a, void* out, size_t n) const override {
        for (const auto& [start, bytes] : regions) {
            if (a >= start && a + n <= start + bytes.size()) { std::memcpy(out, bytes.data() + (a - start), n); return true; }
        }
        return false;
    }
    bool write(uintptr_t a, const void* data, size_t n) override {
        if (fail_writes || ++attempts == fail_on_write_number) return false;
        for (auto& [start, bytes] : regions) {
            if (a >= start && a + n <= start + bytes.size()) { std::memcpy(bytes.data() + (a - start), data, n); ++writes; return true; }
        }
        return false;
    }
    void region(uintptr_t start, size_t size) { regions[start].resize(size, 0); }
    template <typename T> void put(uintptr_t a, const T& v) {
        for (auto& [start, bytes] : regions) {
            if (a >= start && a + sizeof(T) <= start + bytes.size()) { std::memcpy(bytes.data() + (a - start), &v, sizeof(T)); return; }
        }
        FAIL() << "unmapped";
    }
    template <typename T> T get(uintptr_t a) const { T v{}; EXPECT_TRUE(read(a, &v, sizeof(T))); return v; }
};

struct Scene {
    FakeMemory mem;

    Scene() {
        mem.region(kBase, kImageSize);
        mem.region(kWorld, 0x200);
        mem.region(kPlayer, 0x400);
        mem.region(kModel, 0x400);
        mem.region(kEntity, 0x200);
        mem.region(kEntity2, 0x200);
        mem.put<uint64_t>(kBase + kWcmRva, kWorld);
        mem.put<uint64_t>(kWorld + 0x88, kPlayer);
        mem.put<uint64_t>(kPlayer + 0x48, kModel);
        mem.put<uint64_t>(kModel + 0x250, kEntity);
        setClass(kEntity, "SprjAsmModelDrawEntity@NS_SPRJ");
        mem.put<uint32_t>(kEntity + 0x70, 4);
    }

    // Writes a vtable -> RTTI COL -> type descriptor chain naming the class, and points `object` at it.
    void setClass(uintptr_t object, const std::string& name) {
        mem.put<uint64_t>(kBase + kVtableRva - 8, kBase + kColRva);
        mem.put<uint32_t>(kBase + kColRva, 1);              // signature: x64
        mem.put<uint32_t>(kBase + kColRva + 12, kTypeDescRva);
        const std::string full = ".?AV" + name + "@@";
        std::vector<uint8_t> bytes(full.begin(), full.end());
        bytes.push_back(0);
        for (size_t i = 0; i < bytes.size(); ++i) mem.put<uint8_t>(kBase + kTypeDescRva + 16 + i, bytes[i]);
        mem.put<uint64_t>(object, kBase + kVtableRva);
    }
    ModelHider hider() { return ModelHider(mem, mem, kBase, kImageSize, kWcmRva); }
    uint32_t mask() const { return mem.get<uint32_t>(kEntity + 0x70); }
};

} // namespace

TEST(SekiroRttiTest, ReadsTheClassNameThroughVtableAndColAndTypeDescriptor) {
    Scene s;
    const auto name = rttiClassName(s.mem, kBase, kImageSize, kEntity);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "SprjAsmModelDrawEntity@NS_SPRJ");
}

TEST(SekiroRttiTest, NoNameForObjectsWithoutAValidRttiChain) {
    Scene s;
    EXPECT_FALSE(rttiClassName(s.mem, kBase, kImageSize, kModel).has_value());        // first qword is 0
    s.mem.put<uint64_t>(kEntity2, 0x1234);                                             // vtable outside the image
    EXPECT_FALSE(rttiClassName(s.mem, kBase, kImageSize, kEntity2).has_value());
    s.mem.put<uint32_t>(kBase + kColRva, 7);                                           // wrong COL signature
    EXPECT_FALSE(rttiClassName(s.mem, kBase, kImageSize, kEntity).has_value());
    EXPECT_FALSE(rttiClassName(s.mem, kBase, kImageSize, 0xdead0000ull).has_value());  // unreadable
}

TEST(SekiroModelHiderTest, HidesByZeroingTheMaskAndRestoresTheRememberedValue) {
    Scene s;
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mask(), 4u);
    EXPECT_EQ(s.mem.writes, 0);          // visible and wanted visible: no memory traffic at all

    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.mask(), 0u);
    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.mem.writes, 1);          // idempotent

    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mask(), 4u);
    EXPECT_EQ(s.mem.writes, 2);
}

TEST(SekiroModelHiderTest, RefusesAnythingThatIsNotTheExpectedDrawEntity) {
    Scene s;
    s.setClass(kEntity, "ChrModel@NS_SPRJ");
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(true), HideStatus::Rejected);
    EXPECT_EQ(s.mem.writes, 0);
    EXPECT_EQ(s.mask(), 4u);
}

TEST(SekiroModelHiderTest, RefusesAnImplausibleCurrentMask) {
    Scene s;
    s.mem.put<uint32_t>(kEntity + 0x70, 0xbee4f90e); // looks like a float, not a mask: wrong object layout
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(true), HideStatus::Rejected);
    EXPECT_EQ(s.mem.writes, 0);

    s.mem.put<uint32_t>(kEntity + 0x70, 0); // already zero: not a visible state we know how to restore
    EXPECT_EQ(h.update(true), HideStatus::Rejected);
    EXPECT_EQ(s.mem.writes, 0);
}

TEST(SekiroModelHiderTest, NotInWorldWhenAnyLinkInTheChainIsMissing) {
    Scene s;
    ModelHider h = s.hider();
    s.mem.put<uint64_t>(kBase + kWcmRva, 0);
    EXPECT_EQ(h.update(true), HideStatus::NotInWorld);
    s.mem.put<uint64_t>(kBase + kWcmRva, kWorld);
    s.mem.put<uint64_t>(kWorld + 0x88, 0);
    EXPECT_EQ(h.update(true), HideStatus::NotInWorld);
    s.mem.put<uint64_t>(kWorld + 0x88, kPlayer);
    s.mem.put<uint64_t>(kModel + 0x250, 0);
    EXPECT_EQ(h.update(true), HideStatus::NotInWorld);
    EXPECT_EQ(s.mem.writes, 0);
}

TEST(SekiroModelHiderTest, ReappliesWhenTheGameResetsTheMaskWhileHidden) {
    Scene s;
    ModelHider h = s.hider();
    h.update(true);
    s.mem.put<uint32_t>(kEntity + 0x70, 4); // the game redraws the model (e.g. after a cutscene)
    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.mask(), 0u);
    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mask(), 4u);                // still restores the value remembered at the first hide
}

TEST(SekiroModelHiderTest, NeverClobbersAValueTheGameChangedAfterWeRestored) {
    Scene s;
    ModelHider h = s.hider();
    h.update(true);
    h.update(false);
    s.mem.put<uint32_t>(kEntity + 0x70, 6); // game switches to another draw mode while visible
    const int writes = s.mem.writes;
    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mask(), 6u);
    EXPECT_EQ(s.mem.writes, writes);

    h.update(true);                         // hiding again remembers the new value
    h.update(false);
    EXPECT_EQ(s.mask(), 6u);
}

TEST(SekiroModelHiderTest, RestoreIsHarmlessWhenNothingWasHiddenAndPutsTheValueBackOtherwise) {
    Scene s;
    ModelHider h = s.hider();
    h.restore();
    EXPECT_EQ(s.mem.writes, 0);

    h.update(true);
    h.restore();
    EXPECT_EQ(s.mask(), 4u);
    EXPECT_EQ(h.status(), HideStatus::Idle);
}

TEST(SekiroModelHiderTest, ANewEntityAfterAWorldChangeIsHiddenAfreshAndTheOldOneIsNotTouched) {
    Scene s;
    ModelHider h = s.hider();
    h.update(true);
    h.forgetObject(); // world left

    s.setClass(kEntity2, "SprjAsmModelDrawEntity@NS_SPRJ");
    s.mem.put<uint32_t>(kEntity2 + 0x70, 5);
    s.mem.put<uint64_t>(kModel + 0x250, kEntity2);
    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.mem.get<uint32_t>(kEntity2 + 0x70), 0u);
    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mem.get<uint32_t>(kEntity2 + 0x70), 5u);   // the new entity's own value
}

TEST(SekiroModelHiderTest, EntityChangingUnderUsWhileHiddenIsHandledWithoutRestoringIntoTheWrongObject) {
    Scene s;
    ModelHider h = s.hider();
    h.update(true);
    // the game replaced the entity without us noticing a world change
    s.setClass(kEntity2, "SprjAsmModelDrawEntity@NS_SPRJ");
    s.mem.put<uint32_t>(kEntity2 + 0x70, 4);
    s.mem.put<uint64_t>(kModel + 0x250, kEntity2);
    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.mem.get<uint32_t>(kEntity2 + 0x70), 0u);
    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mem.get<uint32_t>(kEntity2 + 0x70), 4u);
}

TEST(SekiroModelHiderTest, ReportsAFailedWriteInsteadOfPretendingToBeHidden) {
    Scene s;
    s.mem.fail_writes = true;
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(true), HideStatus::WriteFailed);
    EXPECT_EQ(s.mask(), 4u);
}

// ---------------------------------------------------------------------------------------------
// Second path (user-verified 2026-10-07): the ChrModel (vtable RVA 0x29F7CE8) itself holds two 64-bit draw
// masks at +0x90 and +0x98, all ones while drawn. Zeroing both hides the mesh while bones, animation and
// physics keep running. It is used next to (or, when the draw entity cannot be found, instead of) the
// SprjAsmModelDrawEntity mask.
// ---------------------------------------------------------------------------------------------
namespace {

constexpr uint32_t kChrModelVtableRva = 0x29F7CE8;
constexpr uint64_t kAllOnes = 0xFFFFFFFFFFFFFFFFull;

struct MaskScene : Scene {
    MaskScene() {
        mem.put<uint64_t>(kModel, kBase + kChrModelVtableRva);
        mem.put<uint64_t>(kModel + 0x90, kAllOnes);
        mem.put<uint64_t>(kModel + 0x98, kAllOnes);
    }
    void dropDrawEntity() { mem.put<uint64_t>(kModel + 0x250, 0); } // the failing real-game case
    uint64_t m1() const { return mem.get<uint64_t>(kModel + 0x90); }
    uint64_t m2() const { return mem.get<uint64_t>(kModel + 0x98); }
};

} // namespace

TEST(SekiroModelMasksTest, HidesThroughTheChrModelMasksWhenTheDrawEntityCannotBeFound) {
    MaskScene s;
    s.dropDrawEntity();
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mem.writes, 0);

    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.m1(), 0u);
    EXPECT_EQ(s.m2(), 0u);
    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.mem.writes, 2) << "idempotent";

    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.m1(), kAllOnes);
    EXPECT_EQ(s.m2(), kAllOnes);
}

TEST(SekiroModelMasksTest, RestoresTheRememberedValuesNotAnAssumedOne) {
    MaskScene s;
    s.dropDrawEntity();
    s.mem.put<uint64_t>(kModel + 0x90, 0x00FF00FF00FF00FFull);
    s.mem.put<uint64_t>(kModel + 0x98, 0x1ull);
    ModelHider h = s.hider();
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    ASSERT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.m1(), 0x00FF00FF00FF00FFull);
    EXPECT_EQ(s.m2(), 0x1ull);
}

TEST(SekiroModelMasksTest, BothPathsAreUsedWhenBothAreAvailableAndBothAreRestored) {
    MaskScene s; // the draw entity is still wired up
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(s.mask(), 0u);
    EXPECT_EQ(s.m1(), 0u);
    EXPECT_EQ(s.m2(), 0u);
    EXPECT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mask(), 4u);
    EXPECT_EQ(s.m1(), kAllOnes);
    EXPECT_EQ(s.m2(), kAllOnes);
}

TEST(SekiroModelMasksTest, OnlyAChrModelByVtableIsEverWritten) {
    MaskScene s;
    s.dropDrawEntity();
    s.mem.put<uint64_t>(kModel, kBase + 0x1111); // something else lives behind player+0x48
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(true), HideStatus::NotInWorld) << "neither path is available";
    EXPECT_EQ(s.mem.writes, 0);
    EXPECT_EQ(s.m1(), kAllOnes);
}

TEST(SekiroModelMasksTest, AMaskPairThatWasAlreadyZeroIsNotOursToRestore) {
    MaskScene s;
    s.dropDrawEntity();
    s.mem.put<uint64_t>(kModel + 0x90, 0);
    s.mem.put<uint64_t>(kModel + 0x98, 0);
    ModelHider h = s.hider();
    EXPECT_EQ(h.update(true), HideStatus::Rejected);
    EXPECT_EQ(s.mem.writes, 0);
}

TEST(SekiroModelMasksTest, AFailedSecondWriteDoesNotLeaveAHalfHiddenModel) {
    MaskScene s;
    s.dropDrawEntity();
    ModelHider h = s.hider();
    s.mem.fail_on_write_number = 2; // the first mask goes through, the second does not
    EXPECT_EQ(h.update(true), HideStatus::WriteFailed);
    EXPECT_EQ(s.m1(), kAllOnes);
    EXPECT_EQ(s.m2(), kAllOnes);
}

TEST(SekiroModelMasksTest, RestoreAndForgetBehaveLikeTheDrawEntityPath) {
    MaskScene s;
    s.dropDrawEntity();
    ModelHider h = s.hider();
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    h.restore();
    EXPECT_EQ(s.m1(), kAllOnes);
    EXPECT_EQ(s.m2(), kAllOnes);
    EXPECT_EQ(h.status(), HideStatus::Idle);

    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    h.forgetObject(); // world left: the model is gone, nothing is written
    const int writes = s.mem.writes;
    EXPECT_EQ(h.status(), HideStatus::NotInWorld);
    h.restore();
    EXPECT_EQ(s.mem.writes, writes);
}

TEST(SekiroModelMasksTest, ANewModelObjectInvalidatesTheRememberedValues) {
    MaskScene s;
    s.dropDrawEntity();
    ModelHider h = s.hider();
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    // the game replaced the Wolf's model with a new object whose masks are visible
    constexpr uintptr_t kModel2 = 0x7ff400500000ull;
    s.mem.region(kModel2, 0x400);
    s.mem.put<uint64_t>(kModel2, kBase + kChrModelVtableRva);
    s.mem.put<uint64_t>(kModel2 + 0x90, 0x7ull);
    s.mem.put<uint64_t>(kModel2 + 0x98, 0x9ull);
    s.mem.put<uint64_t>(kPlayer + 0x48, kModel2);
    ASSERT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.mem.get<uint64_t>(kModel2 + 0x90), 0x7ull) << "never write the old object's values into the new one";
    EXPECT_EQ(s.mem.get<uint64_t>(kModel2 + 0x98), 0x9ull);
}


// ---------------------------------------------------------------------------------------------
// The game resets the masks between our frames (warps, cutscenes, outfit changes). Each reset leaks a frame
// of the Wolf, so the hider counts them and a guard thread re-applies the hide within milliseconds.
// ---------------------------------------------------------------------------------------------
TEST(SekiroModelResetsTest, CountsEveryTimeTheGameResetTheMasksBehindOurBack) {
    MaskScene s;
    s.dropDrawEntity();
    ModelHider h = s.hider();
    EXPECT_EQ(h.resetCount(), 0u);
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(h.resetCount(), 0u) << "the first hide is not a reset";
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(h.resetCount(), 0u) << "nothing happened between the two updates";

    s.mem.put<uint64_t>(kModel + 0x90, kAllOnes); // the game redraws the Wolf
    s.mem.put<uint64_t>(kModel + 0x98, kAllOnes);
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(h.resetCount(), 1u);
    EXPECT_EQ(s.m1(), 0u);
    s.mem.put<uint64_t>(kModel + 0x98, 0x5ull); // only one of the two comes back
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(h.resetCount(), 2u);
    EXPECT_EQ(s.m2(), 0u);
    // restoring afterwards still puts back the values remembered at the very first hide
    ASSERT_EQ(h.update(false), HideStatus::Idle);
    EXPECT_EQ(s.m1(), kAllOnes);
    EXPECT_EQ(s.m2(), kAllOnes);
}

TEST(SekiroModelResetsTest, ANewModelObjectIsAFreshHideNotAReset) {
    MaskScene s;
    s.dropDrawEntity();
    ModelHider h = s.hider();
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    constexpr uintptr_t kModel2 = 0x7ff400500000ull;
    s.mem.region(kModel2, 0x400);
    s.mem.put<uint64_t>(kModel2, kBase + kChrModelVtableRva);
    s.mem.put<uint64_t>(kModel2 + 0x90, kAllOnes);
    s.mem.put<uint64_t>(kModel2 + 0x98, kAllOnes);
    s.mem.put<uint64_t>(kPlayer + 0x48, kModel2);
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(h.resetCount(), 0u);
    EXPECT_EQ(h.objectChangeCount(), 1u) << "a warp swapped the Wolf's model object";
}

TEST(SekiroModelResetsTest, TheDrawEntityMaskResetsAreCountedToo) {
    Scene s; // draw-entity path only
    ModelHider h = s.hider();
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    s.mem.put<uint32_t>(kEntity + 0x70, 4); // redrawn
    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    EXPECT_EQ(h.resetCount(), 1u);
    EXPECT_EQ(s.mask(), 0u);
}


TEST(SekiroModelDiagnosticsTest, DescribesBothHidePathsWithoutWritingAnything) {
    MaskScene s;
    ModelHider h = s.hider();
    const std::string before = h.describe();
    EXPECT_NE(before.find("model="), std::string::npos);
    EXPECT_NE(before.find("masks=ffffffffffffffff,ffffffffffffffff"), std::string::npos) << before;
    EXPECT_NE(before.find("entity="), std::string::npos);
    EXPECT_NE(before.find("entity_mask=4"), std::string::npos) << before;
    EXPECT_EQ(s.mem.writes, 0);

    ASSERT_EQ(h.update(true), HideStatus::Hidden);
    const std::string after = h.describe();
    EXPECT_NE(after.find("masks=0,0"), std::string::npos) << after;
    EXPECT_NE(after.find("entity_mask=0"), std::string::npos) << after;
}

TEST(SekiroModelDiagnosticsTest, SaysWhyAPathIsUnavailable) {
    MaskScene s;
    s.dropDrawEntity();
    s.mem.put<uint64_t>(kModel, kBase + 0x1111);
    ModelHider h = s.hider();
    const std::string text = h.describe();
    EXPECT_NE(text.find("model=wrong-class"), std::string::npos) << text;
    EXPECT_NE(text.find("entity=none"), std::string::npos) << text;
}
