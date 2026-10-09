#include <gtest/gtest.h>

#include "sekiro_host_write.hpp"

#include <cstring>
#include <map>
#include <vector>

using namespace sekiro::live;

namespace {

constexpr uintptr_t kBase = 0x140000000ull;
constexpr size_t kImageSize = 0x4000;
constexpr uintptr_t kEnemy = 0x7ff500000000ull;
constexpr uintptr_t kEnemyContainer = 0x7ff500100000ull;
constexpr uintptr_t kEnemyModule = 0x7ff500200000ull;
constexpr uintptr_t kPlayer = 0x7ff500300000ull;
constexpr uintptr_t kPlayerContainer = 0x7ff500400000ull;
constexpr uintptr_t kPlayerModule = 0x7ff500500000ull;

class FakeMemory : public IMemoryReader, public IMemoryWriter {
public:
    std::map<uintptr_t, std::vector<uint8_t>> regions;
    int writes{0};
    bool fail_writes{false};
    uintptr_t last_write_address{0};

    bool read(uintptr_t a, void* out, size_t n) const override {
        for (const auto& [start, bytes] : regions) {
            if (a >= start && a + n <= start + bytes.size()) {
                std::memcpy(out, bytes.data() + (a - start), n);
                return true;
            }
        }
        return false;
    }
    bool write(uintptr_t a, const void* data, size_t n) override {
        if (fail_writes) return false;
        for (auto& [start, bytes] : regions) {
            if (a >= start && a + n <= start + bytes.size()) {
                std::memcpy(bytes.data() + (a - start), data, n);
                ++writes;
                last_write_address = a;
                return true;
            }
        }
        return false;
    }
    template <typename T>
    void put(uintptr_t a, const T& v) {
        auto& r = regions[a & ~0xFFFFFull];
        if (r.empty()) r.resize(0x100000, 0);
        std::memcpy(r.data() + (a & 0xFFFFF), &v, sizeof(T));
    }
    template <typename T>
    T get(uintptr_t a) const {
        T v{};
        read(a, &v, sizeof(T));
        return v;
    }
};

struct Fixture {
    FakeMemory mem;
    Fixture() {
        mem.put<uint64_t>(kEnemy, kBase + 0x2A27F28); // EnemyIns
        mem.put<uint64_t>(kEnemy + 0x10b8, kEnemyContainer);
        mem.put<uint64_t>(kEnemyContainer + 0x1f8, kEnemyModule);
        mem.put<uint64_t>(kEnemyModule, kBase + 0x2A8BE18); // SprjChrDataModule
        mem.put<uint64_t>(kEnemyModule + 8, kEnemy);        // its owner
        mem.put<int32_t>(kEnemyModule + 0x130, 2101);
        mem.put<int32_t>(kEnemyModule + 0x160, 2101);

        mem.put<uint64_t>(kPlayer, kBase + 0x2A2B338); // PlayerIns
        mem.put<uint64_t>(kPlayer + 0x10b8, kPlayerContainer);
        mem.put<uint64_t>(kPlayerContainer + 0x1e8, kPlayerModule);
        mem.put<uint64_t>(kPlayerModule, kBase + 0x2A8BE18);
        mem.put<uint64_t>(kPlayerModule + 8, kPlayer);
        mem.put<int32_t>(kPlayerModule + 0x130, 800);
        mem.put<int32_t>(kPlayerModule + 0x138, 1120);
    }
};

} // namespace

TEST(SekiroHostWriteTest, LowersAnEnemysHealthByWritingOnlyTheCurrentHpField) {
    Fixture f;
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, 1500.4f), HostHealthWriter::Result::Written);
    EXPECT_EQ(f.mem.get<int32_t>(kEnemyModule + 0x130), 1500);
    EXPECT_EQ(f.mem.writes, 1);
    EXPECT_EQ(f.mem.last_write_address, kEnemyModule + 0x130);
    EXPECT_EQ(f.mem.get<int32_t>(kEnemyModule + 0x160), 2101) << "max hp untouched";
}

TEST(SekiroHostWriteTest, ZeroKillsAndAnEnemyHealthWriteNeverRaisesHealth) {
    Fixture f;
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, 0.f), HostHealthWriter::Result::Written);
    EXPECT_EQ(f.mem.get<int32_t>(kEnemyModule + 0x130), 0);

    Fixture g;
    HostHealthWriter w2(g.mem, g.mem, kBase, kImageSize);
    g.mem.put<int32_t>(kEnemyModule + 0x130, 900);
    EXPECT_EQ(w2.lowerEnemyHealth(kEnemy, 1500.f), HostHealthWriter::Result::Unchanged); // would raise
    EXPECT_EQ(w2.lowerEnemyHealth(kEnemy, 900.f), HostHealthWriter::Result::Unchanged);
    EXPECT_EQ(g.mem.writes, 0);
    EXPECT_EQ(g.mem.get<int32_t>(kEnemyModule + 0x130), 900);
}

TEST(SekiroHostWriteTest, NegativeAndNanTargetsAreClampedOrRefused) {
    Fixture f;
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, std::nanf("")), HostHealthWriter::Result::Rejected);
    EXPECT_EQ(f.mem.writes, 0);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, -50.f), HostHealthWriter::Result::Written); // clamped to 0
    EXPECT_EQ(f.mem.get<int32_t>(kEnemyModule + 0x130), 0);
}

TEST(SekiroHostWriteTest, RefusesToWriteWhenTheObjectIsNotWhatWeThink) {
    for (int which = 0; which < 5; ++which) {
        Fixture f;
        switch (which) {
            case 0: f.mem.put<uint64_t>(kEnemy, kBase + 0x1111); break;                 // wrong class
            case 1: f.mem.put<uint64_t>(kEnemy + 0x10b8, 0); break;                     // no container
            case 2: f.mem.put<uint64_t>(kEnemyContainer + 0x1f8, 0); break;             // no module
            case 3: f.mem.put<uint64_t>(kEnemyModule, kBase + 0x2222); break;           // module of another class
            case 4: f.mem.put<int32_t>(kEnemyModule + 0x160, 0); break;                 // implausible max
        }
        HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
        EXPECT_EQ(w.lowerEnemyHealth(kEnemy, 10.f), HostHealthWriter::Result::Rejected) << "case " << which;
        EXPECT_EQ(f.mem.writes, 0) << "case " << which;
    }
    Fixture g;
    g.mem.put<int32_t>(kEnemyModule + 0x130, 5000); // hp above max: garbage, not a health value
    HostHealthWriter w(g.mem, g.mem, kBase, kImageSize);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, 10.f), HostHealthWriter::Result::Rejected);
    EXPECT_EQ(w.lowerEnemyHealth(0, 10.f), HostHealthWriter::Result::Rejected);
}

TEST(SekiroHostWriteTest, ReportsAFailedWriteInsteadOfPretendingItWorked) {
    Fixture f;
    f.mem.fail_writes = true;
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, 100.f), HostHealthWriter::Result::WriteFailed);
}

TEST(SekiroHostWriteTest, RefundRaisesThePlayersHealthUpToItsMaximumAndNeverLowersIt) {
    Fixture f;
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.raisePlayerHealth(kPlayer, 1000.f), HostHealthWriter::Result::Written);
    EXPECT_EQ(f.mem.get<int32_t>(kPlayerModule + 0x130), 1000);
    EXPECT_EQ(w.raisePlayerHealth(kPlayer, 99999.f), HostHealthWriter::Result::Written); // clamped to max
    EXPECT_EQ(f.mem.get<int32_t>(kPlayerModule + 0x130), 1120);
    EXPECT_EQ(w.raisePlayerHealth(kPlayer, 500.f), HostHealthWriter::Result::Unchanged); // would lower
    EXPECT_EQ(f.mem.get<int32_t>(kPlayerModule + 0x130), 1120);
}

TEST(SekiroHostWriteTest, PlayerRefundIsClassCheckedAndUsesThePlayersMaxHpOffset) {
    Fixture f;
    f.mem.put<uint64_t>(kPlayerContainer + 0x1e8, 0);
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.raisePlayerHealth(kPlayer, 900.f), HostHealthWriter::Result::Rejected);
    Fixture g;
    g.mem.put<uint64_t>(kPlayer, kBase + 0x1111);
    HostHealthWriter w2(g.mem, g.mem, kBase, kImageSize);
    EXPECT_EQ(w2.raisePlayerHealth(kPlayer, 900.f), HostHealthWriter::Result::Rejected);
}


TEST(SekiroHostWriteTest, NeverWritesAModuleThatBelongsToAnotherCharacter) {
    Fixture f;
    f.mem.put<uint64_t>(kEnemyModule + 8, 0x7ff4deadbeef); // this module is somebody else's
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, 10.f), HostHealthWriter::Result::Rejected);
    EXPECT_EQ(f.mem.writes, 0);
    EXPECT_EQ(f.mem.get<int32_t>(kEnemyModule + 0x130), 2101);
}

TEST(SekiroHostWriteTest, WritesThroughAModulePointedAtDirectlyFromTheEnemyObject) {
    Fixture f;
    f.mem.put<uint64_t>(kEnemyContainer + 0x1f8, 0);   // not in the container any more
    f.mem.put<uint64_t>(kEnemy + 0x2288, kEnemyModule); // the usual place for soldiers
    HostHealthWriter w(f.mem, f.mem, kBase, kImageSize);
    EXPECT_EQ(w.lowerEnemyHealth(kEnemy, 1000.f), HostHealthWriter::Result::Written);
    EXPECT_EQ(f.mem.get<int32_t>(kEnemyModule + 0x130), 1000);
}
