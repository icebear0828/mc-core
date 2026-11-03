#pragma once

// Read-only physics probe: formatting, rate limiting and transition detection.
//
// The probe itself (loader_services.cpp) only calls the existing read paths
// (readPhysicsModule / readVitals / readCamera / FallTimer). This header has no
// game dependency, so the sampling rules and the log format are unit-tested
// against plain values on any host.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace eldenring::physprobe {

struct Config {
    uint64_t interval_ms{50}; // 20 Hz by default; mc_er_physprobe.txt can override with interval_ms=<10..1000>
};

struct State {
    uint64_t last_ms{0};
    bool have_sampled{false};

    bool have_prev{false};
    uint8_t ground{0};
    uint8_t falling{0};
    float fall_timer{0.f};
    float pos[3]{};
};

struct Sample {
    uint64_t now_ms{0};
    float dt{0.f};

    bool have_physics{false};
    float pos[3]{};
    float last_pos[3]{};
    float orientation[4]{};
    float v120[3]{};
    float v1b8{0.f};
    uint16_t word90{0};
    uint8_t b91{0};
    uint8_t b92{0};
    uint8_t b93{0};
    uint8_t b1d0{0};
    uint8_t b1d1{0};
    uint8_t b1d5{0};
    uint8_t bcc{0};

    bool have_vitals{false};
    int hp{0};
    int max_hp{0};

    float fall_timer{0.f};

    bool have_camera{false};
    float cam_pos[3]{};
    float cam_fwd[3]{};
    float cam_up[3]{};
    float cam_right[3]{};
    float fov_y{0.f};
};

// True when this frame should be written. The first call always samples; after
// that, at most one line per interval_ms.
inline bool shouldSample(State& st, uint64_t now_ms, const Config& cfg) {
    if (st.have_sampled && now_ms - st.last_ms < cfg.interval_ms) return false;
    st.last_ms = now_ms;
    st.have_sampled = true;
    return true;
}

inline float distance3(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A jump larger than this between two samples is a teleport / floating-origin
// re-base, not ordinary movement (ordinary movement is < 1 m per 50 ms).
inline constexpr float kJumpThresholdMetres = 5.f;

struct Event {
    bool ground_changed{false};
    bool falling_changed{false};
    bool fall_timer_reset{false};
    bool position_jump{false};

    uint8_t prev_ground{0};
    uint8_t ground{0};
    uint8_t prev_falling{0};
    uint8_t falling{0};
    float prev_fall_timer{0.f};
    float fall_timer{0.f};
    float jump_distance{0.f};

    [[nodiscard]] bool any() const { return ground_changed || falling_changed || fall_timer_reset || position_jump; }
};

inline Event detectEvent(const State& st, const Sample& s) {
    Event e;
    if (!st.have_prev || !s.have_physics) return e;

    e.prev_ground = st.ground;
    e.ground = s.b92;
    e.ground_changed = st.ground != s.b92;

    e.prev_falling = st.falling;
    e.falling = s.b1d0;
    e.falling_changed = st.falling != s.b1d0;

    e.prev_fall_timer = st.fall_timer;
    e.fall_timer = s.fall_timer;
    e.fall_timer_reset = st.fall_timer > 0.5f && s.fall_timer < 0.05f;

    e.jump_distance = distance3(st.pos, s.pos);
    e.position_jump = e.jump_distance > kJumpThresholdMetres;
    return e;
}

inline void commit(State& st, const Sample& s) {
    if (!s.have_physics) return;
    st.have_prev = true;
    st.ground = s.b92;
    st.falling = s.b1d0;
    st.fall_timer = s.fall_timer;
    std::memcpy(st.pos, s.pos, sizeof(st.pos));
}

inline std::string formatSample(const Sample& s) {
    char buf[1024];
    const double dt = static_cast<double>(s.dt);
    std::snprintf(
        buf, sizeof(buf),
        "t=%llu dt=%.4f pos=(%.3f %.3f %.3f) last=(%.3f %.3f %.3f) d=(%.3f %.3f %.3f) "
        "q=(%.4f %.4f %.4f %.4f) v120=(%.3f %.3f %.3f) v1b8=%.3f "
        "b90=0x%04X b91=%u b92=%u b93=%u b1d0=%u b1d1=%u b1d5=%u bcc=%u "
        "hp=%d/%d fall_t=%.3f cam=(%.3f %.3f %.3f) fwd=(%.3f %.3f %.3f) "
        "up=(%.3f %.3f %.3f) fov=%.4f",
        static_cast<unsigned long long>(s.now_ms), dt,
        static_cast<double>(s.pos[0]), static_cast<double>(s.pos[1]), static_cast<double>(s.pos[2]),
        static_cast<double>(s.last_pos[0]), static_cast<double>(s.last_pos[1]), static_cast<double>(s.last_pos[2]),
        static_cast<double>(s.pos[0] - s.last_pos[0]), static_cast<double>(s.pos[1] - s.last_pos[1]),
        static_cast<double>(s.pos[2] - s.last_pos[2]),
        static_cast<double>(s.orientation[0]), static_cast<double>(s.orientation[1]),
        static_cast<double>(s.orientation[2]), static_cast<double>(s.orientation[3]),
        static_cast<double>(s.v120[0]), static_cast<double>(s.v120[1]), static_cast<double>(s.v120[2]),
        static_cast<double>(s.v1b8),
        static_cast<unsigned>(s.word90), static_cast<unsigned>(s.b91), static_cast<unsigned>(s.b92),
        static_cast<unsigned>(s.b93), static_cast<unsigned>(s.b1d0), static_cast<unsigned>(s.b1d1),
        static_cast<unsigned>(s.b1d5), static_cast<unsigned>(s.bcc),
        s.hp, s.max_hp, static_cast<double>(s.fall_timer),
        static_cast<double>(s.cam_pos[0]), static_cast<double>(s.cam_pos[1]), static_cast<double>(s.cam_pos[2]),
        static_cast<double>(s.cam_fwd[0]), static_cast<double>(s.cam_fwd[1]), static_cast<double>(s.cam_fwd[2]),
        static_cast<double>(s.cam_up[0]), static_cast<double>(s.cam_up[1]), static_cast<double>(s.cam_up[2]),
        static_cast<double>(s.fov_y));
    return buf;
}

inline std::string formatEvent(const Event& e, const Sample& s) {
    char buf[512];
    std::snprintf(
        buf, sizeof(buf),
        "EVENT t=%llu ground=%u->%u falling=%u->%u fall_t=%.3f->%.3f jump=%.3f pos=(%.3f %.3f %.3f)",
        static_cast<unsigned long long>(s.now_ms),
        static_cast<unsigned>(e.prev_ground), static_cast<unsigned>(e.ground),
        static_cast<unsigned>(e.prev_falling), static_cast<unsigned>(e.falling),
        static_cast<double>(e.prev_fall_timer), static_cast<double>(e.fall_timer),
        static_cast<double>(e.jump_distance),
        static_cast<double>(s.pos[0]), static_cast<double>(s.pos[1]), static_cast<double>(s.pos[2]));
    return buf;
}

} // namespace eldenring::physprobe
