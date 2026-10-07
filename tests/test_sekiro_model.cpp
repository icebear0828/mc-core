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

    bool read(uintptr_t a, void* out, size_t n) const override {
        for (const auto& [start, bytes] : regions) {
            if (a >= start && a + n <= start + bytes.size()) { std::memcpy(out, bytes.data() + (a - start), n); return true; }
        }
        return false;
    }
    bool write(uintptr_t a, const void* data, size_t n) override {
        if (fail_writes) return false;
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
