#include "eldenring_perfstats.hpp"

#include <gtest/gtest.h>

#include <mutex>
#include <string>

namespace {

int g_logged_lines = 0;

void CountLoggedLine(const char*) {
    ++g_logged_lines;
}

} // namespace

TEST(EldenringPerfStats, DisabledCountsNothing) {
    eldenring::perfstats::resetForTesting();
    eldenring::perfstats::noteRead(true);
    eldenring::perfstats::noteRead(false);
    eldenring::perfstats::noteSlowRtti();
    const eldenring::perfstats::Delta d = eldenring::perfstats::takeDelta();
    EXPECT_EQ(d.read_calls, 0u);
    EXPECT_EQ(d.read_failures, 0u);
    EXPECT_EQ(d.slow_rtti, 0u);
}

TEST(EldenringPerfStats, EnabledCountsEveryCategory) {
    eldenring::perfstats::resetForTesting();
    eldenring::perfstats::setEnabled(true);
    eldenring::perfstats::noteRead(true);
    eldenring::perfstats::noteRead(false);
    eldenring::perfstats::noteSlowRtti();
    eldenring::perfstats::noteRttiCacheHit();
    eldenring::perfstats::noteEnumerate();
    eldenring::perfstats::noteEnumerateSlots(17);
    eldenring::perfstats::noteFindGrace();
    eldenring::perfstats::noteFindGraceSlots(9);
    eldenring::perfstats::noteUpdateNativeModel();
    eldenring::perfstats::noteCastRay(1500);

    const eldenring::perfstats::Delta d = eldenring::perfstats::takeDelta();
    EXPECT_EQ(d.read_calls, 2u);
    EXPECT_EQ(d.read_failures, 1u);
    EXPECT_EQ(d.slow_rtti, 1u);
    EXPECT_EQ(d.rtti_cache_hits, 1u);
    EXPECT_EQ(d.enumerate_calls, 1u);
    EXPECT_EQ(d.enumerate_slots, 17u);
    EXPECT_EQ(d.find_grace_calls, 1u);
    EXPECT_EQ(d.find_grace_slots, 9u);
    EXPECT_EQ(d.update_native_model, 1u);
    EXPECT_EQ(d.cast_ray_calls, 1u);
    EXPECT_EQ(d.cast_ray_ns, 1500u);
    eldenring::perfstats::setEnabled(false);
}

TEST(EldenringPerfStats, TakeDeltaResetsCounters) {
    eldenring::perfstats::resetForTesting();
    eldenring::perfstats::setEnabled(true);
    eldenring::perfstats::noteSlowRtti();
    const eldenring::perfstats::Delta first = eldenring::perfstats::takeDelta();
    const eldenring::perfstats::Delta second = eldenring::perfstats::takeDelta();
    EXPECT_EQ(first.slow_rtti, 1u);
    EXPECT_EQ(second.slow_rtti, 0u);
    eldenring::perfstats::setEnabled(false);
}

TEST(EldenringPerfStats, FormatDeltaContainsKeys) {
    eldenring::perfstats::Delta d;
    d.read_calls = 10;
    d.cast_ray_calls = 2;
    d.cast_ray_ns = 4000;
    char line[640] = {};
    eldenring::perfstats::formatDelta(d, line, sizeof(line));
    const std::string text(line);
    EXPECT_NE(text.find("read=10"), std::string::npos);
    EXPECT_NE(text.find("ray=2 2.0us/call"), std::string::npos);
    EXPECT_NE(text.find("blocks_lock"), std::string::npos);
    EXPECT_NE(text.find("melee_lock"), std::string::npos);
}

TEST(EldenringPerfStats, ScopedLockReleasesTheMutex) {
    eldenring::perfstats::resetForTesting();
    eldenring::perfstats::setEnabled(true);
    std::mutex mutex;
    {
        eldenring::perfstats::ScopedTimedLock lock(mutex, eldenring::perfstats::LockKind::Blocks);
    }
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
    eldenring::perfstats::setEnabled(false);
}

TEST(EldenringPerfStats, ReportIfDueRateLimitsToOncePerSecond) {
    eldenring::perfstats::resetForTesting();
    eldenring::perfstats::setEnabled(true);
    g_logged_lines = 0;
    eldenring::perfstats::reportIfDue(1000, &CountLoggedLine);
    EXPECT_EQ(g_logged_lines, 1);
    eldenring::perfstats::reportIfDue(1500, &CountLoggedLine);
    EXPECT_EQ(g_logged_lines, 1);
    eldenring::perfstats::reportIfDue(2001, &CountLoggedLine);
    EXPECT_EQ(g_logged_lines, 2);
    eldenring::perfstats::setEnabled(false);
}

TEST(EldenringPerfStats, DisabledReportDoesNothing) {
    eldenring::perfstats::resetForTesting();
    g_logged_lines = 0;
    eldenring::perfstats::reportIfDue(1000, &CountLoggedLine);
    EXPECT_EQ(g_logged_lines, 0);
}
