#pragma once

// Opt-in per-second performance counters for the Elden Ring adapter.
//
// Enable with a file named mc_er_perfstats.txt in the game folder. When off,
// every hook is one relaxed atomic load plus a branch. Counters are reported
// as per-second deltas so a "before" and an "after" run can be compared.
//
// Header-only on purpose: the same header is used by the Windows adapter and
// by the platform-independent unit tests.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace eldenring::perfstats {

struct Counters {
    std::atomic<uint64_t> read_calls{0};
    std::atomic<uint64_t> read_failures{0};
    std::atomic<uint64_t> slow_rtti{0};
    std::atomic<uint64_t> rtti_cache_hits{0};
    std::atomic<uint64_t> enumerate_calls{0};
    std::atomic<uint64_t> enumerate_slots{0};
    std::atomic<uint64_t> find_grace_calls{0};
    std::atomic<uint64_t> find_grace_slots{0};
    std::atomic<uint64_t> update_native_model{0};
    std::atomic<uint64_t> cast_ray_calls{0};
    std::atomic<uint64_t> cast_ray_ns{0};
    std::atomic<uint64_t> blocks_lock_wait_ns{0};
    std::atomic<uint64_t> blocks_lock_hold_ns{0};
    std::atomic<uint64_t> blocks_lock_max_hold_ns{0};
    std::atomic<uint64_t> melee_lock_wait_ns{0};
    std::atomic<uint64_t> melee_lock_hold_ns{0};
    std::atomic<uint64_t> melee_lock_max_hold_ns{0};
};

struct Delta {
    uint64_t read_calls{0};
    uint64_t read_failures{0};
    uint64_t slow_rtti{0};
    uint64_t rtti_cache_hits{0};
    uint64_t enumerate_calls{0};
    uint64_t enumerate_slots{0};
    uint64_t find_grace_calls{0};
    uint64_t find_grace_slots{0};
    uint64_t update_native_model{0};
    uint64_t cast_ray_calls{0};
    uint64_t cast_ray_ns{0};
    uint64_t blocks_lock_wait_ns{0};
    uint64_t blocks_lock_hold_ns{0};
    uint64_t blocks_lock_max_hold_ns{0};
    uint64_t melee_lock_wait_ns{0};
    uint64_t melee_lock_hold_ns{0};
    uint64_t melee_lock_max_hold_ns{0};
};

inline Counters& counters() {
    static Counters c;
    return c;
}

inline std::atomic<bool>& enabledFlag() {
    static std::atomic<bool> enabled{false};
    return enabled;
}

inline std::atomic<uint64_t>& nextReportMs() {
    static std::atomic<uint64_t> next{0};
    return next;
}

inline bool on() noexcept {
    return enabledFlag().load(std::memory_order_relaxed);
}

inline void setEnabled(bool enabled) noexcept {
    enabledFlag().store(enabled, std::memory_order_relaxed);
}

inline uint64_t nowNs() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

inline void noteRead(bool ok) noexcept {
    if (!on()) return;
    Counters& c = counters();
    c.read_calls.fetch_add(1, std::memory_order_relaxed);
    if (!ok) c.read_failures.fetch_add(1, std::memory_order_relaxed);
}

inline void noteSlowRtti() noexcept {
    if (on()) counters().slow_rtti.fetch_add(1, std::memory_order_relaxed);
}

inline void noteRttiCacheHit() noexcept {
    if (on()) counters().rtti_cache_hits.fetch_add(1, std::memory_order_relaxed);
}

inline void noteEnumerate() noexcept {
    if (on()) counters().enumerate_calls.fetch_add(1, std::memory_order_relaxed);
}

inline void noteEnumerateSlots(uint64_t slots) noexcept {
    if (on() && slots != 0) counters().enumerate_slots.fetch_add(slots, std::memory_order_relaxed);
}

inline void noteFindGrace() noexcept {
    if (on()) counters().find_grace_calls.fetch_add(1, std::memory_order_relaxed);
}

inline void noteFindGraceSlots(uint64_t slots) noexcept {
    if (on() && slots != 0) counters().find_grace_slots.fetch_add(slots, std::memory_order_relaxed);
}

inline void noteUpdateNativeModel() noexcept {
    if (on()) counters().update_native_model.fetch_add(1, std::memory_order_relaxed);
}

inline void noteCastRay(uint64_t elapsed_ns) noexcept {
    if (!on()) return;
    Counters& c = counters();
    c.cast_ray_calls.fetch_add(1, std::memory_order_relaxed);
    c.cast_ray_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
}

inline void bumpMax(std::atomic<uint64_t>& slot, uint64_t value) noexcept {
    uint64_t current = slot.load(std::memory_order_relaxed);
    while (value > current && !slot.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

enum class LockKind { Blocks, Melee };

// Times one std::mutex scope. Wait is the time spent blocked on lock();
// hold is the time between acquisition and the end of the scope.
class ScopedTimedLock {
public:
    ScopedTimedLock(std::mutex& mutex, LockKind kind) : mutex_(mutex), kind_(kind) {
        if (!on()) {
            mutex_.lock();
            return;
        }
        const uint64_t start = nowNs();
        mutex_.lock();
        const uint64_t locked = nowNs();
        locked_at_ = locked;
        timed_ = true;
        Counters& c = counters();
        if (kind_ == LockKind::Blocks) {
            c.blocks_lock_wait_ns.fetch_add(locked - start, std::memory_order_relaxed);
        } else {
            c.melee_lock_wait_ns.fetch_add(locked - start, std::memory_order_relaxed);
        }
    }

    ~ScopedTimedLock() {
        if (timed_) {
            const uint64_t held = nowNs() - locked_at_;
            Counters& c = counters();
            if (kind_ == LockKind::Blocks) {
                c.blocks_lock_hold_ns.fetch_add(held, std::memory_order_relaxed);
                bumpMax(c.blocks_lock_max_hold_ns, held);
            } else {
                c.melee_lock_hold_ns.fetch_add(held, std::memory_order_relaxed);
                bumpMax(c.melee_lock_max_hold_ns, held);
            }
        }
        mutex_.unlock();
    }

    ScopedTimedLock(const ScopedTimedLock&) = delete;
    ScopedTimedLock& operator=(const ScopedTimedLock&) = delete;
    ScopedTimedLock(ScopedTimedLock&&) = delete;
    ScopedTimedLock& operator=(ScopedTimedLock&&) = delete;

private:
    std::mutex& mutex_;
    LockKind kind_;
    uint64_t locked_at_{0};
    bool timed_{false};
};

inline Delta takeDelta() noexcept {
    Counters& c = counters();
    Delta d;
    d.read_calls = c.read_calls.exchange(0, std::memory_order_relaxed);
    d.read_failures = c.read_failures.exchange(0, std::memory_order_relaxed);
    d.slow_rtti = c.slow_rtti.exchange(0, std::memory_order_relaxed);
    d.rtti_cache_hits = c.rtti_cache_hits.exchange(0, std::memory_order_relaxed);
    d.enumerate_calls = c.enumerate_calls.exchange(0, std::memory_order_relaxed);
    d.enumerate_slots = c.enumerate_slots.exchange(0, std::memory_order_relaxed);
    d.find_grace_calls = c.find_grace_calls.exchange(0, std::memory_order_relaxed);
    d.find_grace_slots = c.find_grace_slots.exchange(0, std::memory_order_relaxed);
    d.update_native_model = c.update_native_model.exchange(0, std::memory_order_relaxed);
    d.cast_ray_calls = c.cast_ray_calls.exchange(0, std::memory_order_relaxed);
    d.cast_ray_ns = c.cast_ray_ns.exchange(0, std::memory_order_relaxed);
    d.blocks_lock_wait_ns = c.blocks_lock_wait_ns.exchange(0, std::memory_order_relaxed);
    d.blocks_lock_hold_ns = c.blocks_lock_hold_ns.exchange(0, std::memory_order_relaxed);
    d.blocks_lock_max_hold_ns = c.blocks_lock_max_hold_ns.exchange(0, std::memory_order_relaxed);
    d.melee_lock_wait_ns = c.melee_lock_wait_ns.exchange(0, std::memory_order_relaxed);
    d.melee_lock_hold_ns = c.melee_lock_hold_ns.exchange(0, std::memory_order_relaxed);
    d.melee_lock_max_hold_ns = c.melee_lock_max_hold_ns.exchange(0, std::memory_order_relaxed);
    return d;
}

inline void formatDelta(const Delta& d, char* out, std::size_t capacity) noexcept {
    const double ray_us =
        d.cast_ray_calls != 0 ? static_cast<double>(d.cast_ray_ns) / (1000.0 * static_cast<double>(d.cast_ray_calls)) : 0.0;
    const double blocks_wait_ms = static_cast<double>(d.blocks_lock_wait_ns) / 1e6;
    const double blocks_hold_ms = static_cast<double>(d.blocks_lock_hold_ns) / 1e6;
    const double blocks_max_ms = static_cast<double>(d.blocks_lock_max_hold_ns) / 1e6;
    const double melee_wait_ms = static_cast<double>(d.melee_lock_wait_ns) / 1e6;
    const double melee_hold_ms = static_cast<double>(d.melee_lock_hold_ns) / 1e6;
    const double melee_max_ms = static_cast<double>(d.melee_lock_max_hold_ns) / 1e6;
    (void)std::snprintf(
        out, capacity,
        "perfstats/s: read=%llu fail=%llu slow_rtti=%llu rtti_hit=%llu enum=%llu (%llu slots) grace=%llu (%llu slots) unm=%llu "
        "ray=%llu %.1fus/call | blocks_lock wait=%.3fms hold=%.3fms max=%.3fms | melee_lock wait=%.3fms hold=%.3fms max=%.3fms",
        static_cast<unsigned long long>(d.read_calls), static_cast<unsigned long long>(d.read_failures),
        static_cast<unsigned long long>(d.slow_rtti), static_cast<unsigned long long>(d.rtti_cache_hits),
        static_cast<unsigned long long>(d.enumerate_calls), static_cast<unsigned long long>(d.enumerate_slots),
        static_cast<unsigned long long>(d.find_grace_calls), static_cast<unsigned long long>(d.find_grace_slots),
        static_cast<unsigned long long>(d.update_native_model), static_cast<unsigned long long>(d.cast_ray_calls), ray_us,
        blocks_wait_ms, blocks_hold_ms, blocks_max_ms, melee_wait_ms, melee_hold_ms, melee_max_ms);
}

// Reports at most once per second. Safe to call from several threads: only the
// thread that advances the deadline formats and logs.
inline void reportIfDue(uint64_t now_ms, void (*log_line)(const char*)) {
    if (!on() || log_line == nullptr) return;
    std::atomic<uint64_t>& next = nextReportMs();
    uint64_t expected = next.load(std::memory_order_relaxed);
    if (now_ms < expected) return;
    if (!next.compare_exchange_strong(expected, now_ms + 1000, std::memory_order_relaxed)) return;
    const Delta d = takeDelta();
    char line[640];
    formatDelta(d, line, sizeof(line));
    log_line(line);
}

inline void resetForTesting() noexcept {
    (void)takeDelta();
    nextReportMs().store(0, std::memory_order_relaxed);
    enabledFlag().store(false, std::memory_order_relaxed);
}

} // namespace eldenring::perfstats
