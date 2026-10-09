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
