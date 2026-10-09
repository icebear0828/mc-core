#include <gtest/gtest.h>

#include "eldenring_buddy.hpp"

#include <cstring>
#include <map>
#include <vector>

using namespace eldenring;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr uintptr_t kWorld = 0x7ff500000000ull;
constexpr uintptr_t kMan = 0x7ff600000000ull;

class FakeMemory : public live::IMemoryReader {
public:
    std::map<uintptr_t, std::vector<uint8_t>> regions;
    mutable int reads{0};
    bool read(uintptr_t address, void* out, size_t size) const override {
        ++reads;
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

FakeMemory world(int32_t request = -1, int32_t active = -1, int32_t tablet = -1, int32_t busy = 0) {
    FakeMemory m;
    m.region(kBase + live::layout::kWorldChrManGlobalRva, 8);
    m.put<uint64_t>(kBase + live::layout::kWorldChrManGlobalRva, kWorld);
    m.region(kWorld + buddy::layout::kBuddyManInWorldChrMan, 8);
    m.put<uint64_t>(kWorld + buddy::layout::kBuddyManInWorldChrMan, kMan);
    m.region(kMan, 0x200);
    m.put<uint64_t>(kMan, kBase + 0x2A4F828);
    m.put(kMan + buddy::layout::kRequestId, request);
    m.put(kMan + buddy::layout::kActiveId, active);
    m.put(kMan + buddy::layout::kTabletId, tablet);
    m.put(kMan + buddy::layout::kBusyCount, busy);
    return m;
}

} // namespace

TEST(Buddy, ReadsTheTrackedFieldsAndTheVtable) {
    auto m = world(21200000, 7, 10000100, 2);
    const auto s = buddy::sample(m, kBase);
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.buddy_man, kMan);
    EXPECT_EQ(s.request, 21200000);
    EXPECT_EQ(s.active, 7);
    EXPECT_EQ(s.tablet, 10000100);
    EXPECT_EQ(s.busy, 2);
    EXPECT_EQ(s.vtable_rva, 0x2A4F828u);
}

TEST(Buddy, NegativeOnesAreReadAsSigned) {
    auto m = world();
    const auto s = buddy::sample(m, kBase);
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.request, -1);
    EXPECT_EQ(s.tablet, -1);
}

TEST(Buddy, InvalidWhenTheWorldOrTheManagerIsMissing) {
    FakeMemory none;
    EXPECT_FALSE(buddy::sample(none, kBase).valid); // global unreadable

    auto m = world();
    m.put<uint64_t>(kBase + live::layout::kWorldChrManGlobalRva, 0);
    EXPECT_FALSE(buddy::sample(m, kBase).valid); // not in a world

    auto m2 = world();
    m2.put<uint64_t>(kWorld + buddy::layout::kBuddyManInWorldChrMan, 0);
    EXPECT_FALSE(buddy::sample(m2, kBase).valid); // no manager yet

    auto m3 = world();
    m3.regions.erase(kMan); // pointer set but the memory is unreadable
    EXPECT_FALSE(buddy::sample(m3, kBase).valid);
}

TEST(Buddy, ARandomFirstQwordIsNotReportedAsAnImageVtable) {
    auto m = world();
    m.put<uint64_t>(kMan, 0x1234ull);
    EXPECT_EQ(buddy::sample(m, kBase).vtable_rva, 0u);
}

TEST(Buddy, MonitorLogsTheFirstSampleThenOnlyChanges) {
    auto m = world();
    buddy::Monitor mon;
    const std::string first = mon.update(buddy::sample(m, kBase));
    EXPECT_NE(first.find("(first)"), std::string::npos);
    EXPECT_NE(first.find("request=-1"), std::string::npos);
    EXPECT_TRUE(mon.update(buddy::sample(m, kBase)).empty());
    EXPECT_TRUE(mon.update(buddy::sample(m, kBase)).empty());

    m.put(kMan + buddy::layout::kRequestId, 21200000);
    const std::string req = mon.update(buddy::sample(m, kBase));
    EXPECT_NE(req.find("request=21200000"), std::string::npos);
    EXPECT_EQ(req.find("(first)"), std::string::npos);
    EXPECT_TRUE(mon.update(buddy::sample(m, kBase)).empty());

    m.put(kMan + buddy::layout::kRequestId, -1);
    m.put(kMan + buddy::layout::kActiveId, 21200000);
    const std::string act = mon.update(buddy::sample(m, kBase));
    EXPECT_NE(act.find("request=-1 active=21200000"), std::string::npos);
}

TEST(Buddy, UntrackedBytesChangingDoNotSpamTheLog) {
    auto m = world();
    buddy::Monitor mon;
    (void)mon.update(buddy::sample(m, kBase));
    m.put<uint32_t>(kMan + 0x50, 0xDEADBEEF);
    EXPECT_TRUE(mon.update(buddy::sample(m, kBase)).empty());
    // ...but the raw dump on the next real change shows them.
    m.put(kMan + buddy::layout::kBusyCount, 1);
    EXPECT_NE(mon.update(buddy::sample(m, kBase)).find("DEADBEEF"), std::string::npos);
}

TEST(Buddy, MonitorReportsLeavingAndEnteringAWorld) {
    auto m = world();
    buddy::Monitor mon;
    (void)mon.update(buddy::sample(m, kBase));
    const uint64_t saved = kWorld;
    m.put<uint64_t>(kBase + live::layout::kWorldChrManGlobalRva, 0);
    EXPECT_EQ(mon.update(buddy::sample(m, kBase)), "buddy: became unavailable");
    EXPECT_TRUE(mon.update(buddy::sample(m, kBase)).empty());
    m.put<uint64_t>(kBase + live::layout::kWorldChrManGlobalRva, saved);
    EXPECT_FALSE(mon.update(buddy::sample(m, kBase)).empty());
}

TEST(Buddy, SamplingOnlyReads) {
    auto m = world();
    const auto before = m.regions;
    (void)buddy::sample(m, kBase);
    EXPECT_EQ(m.regions, before);
}

TEST(Buddy, TracksTheCountAtPlus80ThatWasObservedToGoFromZeroToOneWhileSummoned) {
    auto m = world();
    buddy::Monitor mon;
    (void)mon.update(buddy::sample(m, kBase));
    m.put<int32_t>(kMan + buddy::layout::kSummonedFlag, 1);
    const auto s = buddy::sample(m, kBase);
    EXPECT_EQ(s.summoned, 1);
    EXPECT_NE(mon.update(s).find("summoned=1"), std::string::npos);
}

// ---- summon plan: what the experiment writes, and when it refuses --------------------------------------------

TEST(BuddySummon, PlansTheTabletFirstThenTheRequestAtTheObservedOffsets) {
    auto m = world();
    m.put<int32_t>(kMan + buddy::layout::kSummonedFlag, 1);
    const auto plan = buddy::planSummon(buddy::sample(m, kBase), 232000, 1042360100);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->tablet_address, kMan + 0x3C);
    EXPECT_EQ(plan->request_address, kMan + 0x20);
    EXPECT_EQ(plan->tablet, 1042360100);
    EXPECT_EQ(plan->request, 232000);
    EXPECT_STREQ(buddy::refusalReason(buddy::sample(m, kBase)), "");
}

TEST(BuddySummon, RefusesWhenNotInAWorldOrTheWorldIsNotUp) {
    FakeMemory none;
    EXPECT_FALSE(buddy::planSummon(buddy::sample(none, kBase), 232000, 1).has_value());
    auto m = world(); // +0x80 == 0: loading / title
    EXPECT_FALSE(buddy::planSummon(buddy::sample(m, kBase), 232000, 1).has_value());
    EXPECT_STRNE(buddy::refusalReason(buddy::sample(m, kBase)), "");
}

TEST(BuddySummon, RefusesWhileARequestIsPendingOrTheManagerIsBusy) {
    auto m = world(232000);
    m.put<int32_t>(kMan + buddy::layout::kSummonedFlag, 1);
    EXPECT_FALSE(buddy::planSummon(buddy::sample(m, kBase), 232000, 1).has_value());
    auto b = world(-1, -1, 0, 2);
    b.put<int32_t>(kMan + buddy::layout::kSummonedFlag, 1);
    EXPECT_FALSE(buddy::planSummon(buddy::sample(b, kBase), 232000, 1).has_value());
}

TEST(BuddySummon, RefusesNonsenseIds) {
    auto m = world();
    m.put<int32_t>(kMan + buddy::layout::kSummonedFlag, 1);
    const auto snap = buddy::sample(m, kBase);
    EXPECT_FALSE(buddy::planSummon(snap, -1, 1042360100).has_value());
    EXPECT_FALSE(buddy::planSummon(snap, 0, 1042360100).has_value());
    EXPECT_FALSE(buddy::planSummon(snap, 232000, 0).has_value());
}

TEST(BuddySummon, NewEntitiesAreTheOnesMissingFromTheBaseline) {
    const std::vector<uintptr_t> before{0x10, 0x20, 0x30};
    const std::vector<uintptr_t> after{0x10, 0x40, 0x30, 0x50, 0x20};
    const auto fresh = buddy::newEntities(before, after);
    ASSERT_EQ(fresh.size(), 2u);
    EXPECT_EQ(fresh[0], 1u); // indices into `after`
    EXPECT_EQ(fresh[1], 3u);
    EXPECT_TRUE(buddy::newEntities(after, after).empty());
    EXPECT_EQ(buddy::newEntities({}, after).size(), after.size());
}

TEST(Buddy, ReadsTheSpawnPointAndHeadingTheSpawnFunctionUses) {
    // 0x1404BBDD0 adds [man+0xA0..0xAC] to each unit's offset, rotated by the angle at [man+0xB0].
    auto m = world();
    const float pos[4] = {101.5f, 22.25f, -7.75f, 1.0f};
    for (int i = 0; i < 4; ++i) m.put<float>(kMan + 0xA0 + 4 * i, pos[i]);
    m.put<float>(kMan + 0xB0, 1.5f);
    const auto s = buddy::sample(m, kBase);
    ASSERT_TRUE(s.valid);
    EXPECT_FLOAT_EQ(s.spawn_pos[0], 101.5f);
    EXPECT_FLOAT_EQ(s.spawn_pos[1], 22.25f);
    EXPECT_FLOAT_EQ(s.spawn_pos[2], -7.75f);
    EXPECT_FLOAT_EQ(s.spawn_pos[3], 1.0f);
    EXPECT_FLOAT_EQ(s.spawn_yaw, 1.5f);
    buddy::Monitor mon;
    const std::string text = mon.update(s);
    EXPECT_NE(text.find("spawn=(101.50 22.25 -7.75 1.00) yaw=1.50"), std::string::npos) << text;
}

TEST(Buddy, TheRawDumpCoversTheSpawnPointToo) {
    auto m = world();
    m.put<uint32_t>(kMan + 0xB0, 0xCAFEF00D);
    buddy::Monitor mon;
    EXPECT_NE(mon.update(buddy::sample(m, kBase)).find("CAFEF00D"), std::string::npos);
}
