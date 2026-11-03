#include <gtest/gtest.h>

#include "eldenring_physprobe.hpp"

using eldenring::physprobe::Config;
using eldenring::physprobe::Sample;
using eldenring::physprobe::State;

namespace {

Sample physicsSample(float x, float y, float z) {
    Sample s;
    s.have_physics = true;
    s.pos[0] = x;
    s.pos[1] = y;
    s.pos[2] = z;
    return s;
}

} // namespace

TEST(PhysProbeTest, SamplesAtTheConfiguredInterval) {
    State st;
    Config cfg;
    cfg.interval_ms = 50;

    EXPECT_TRUE(eldenring::physprobe::shouldSample(st, 1000, cfg));
    EXPECT_FALSE(eldenring::physprobe::shouldSample(st, 1020, cfg));
    EXPECT_FALSE(eldenring::physprobe::shouldSample(st, 1049, cfg));
    EXPECT_TRUE(eldenring::physprobe::shouldSample(st, 1050, cfg));
}

TEST(PhysProbeTest, FirstSampleHasNoTransition) {
    State st;
    Sample s = physicsSample(1.f, 2.f, 3.f);
    s.b92 = 1;
    s.b1d0 = 0;

    const auto e = eldenring::physprobe::detectEvent(st, s);
    EXPECT_FALSE(e.any());
}

TEST(PhysProbeTest, DetectsGroundAndFallingChanges) {
    State st;
    Sample s = physicsSample(1.f, 2.f, 3.f);
    s.b92 = 1;
    s.b1d0 = 0;
    eldenring::physprobe::commit(st, s);

    s.b92 = 0;
    s.b1d0 = 1;
    const auto e = eldenring::physprobe::detectEvent(st, s);
    EXPECT_TRUE(e.ground_changed);
    EXPECT_TRUE(e.falling_changed);
    EXPECT_EQ(e.prev_ground, 1);
    EXPECT_EQ(e.ground, 0);
    EXPECT_EQ(e.prev_falling, 0);
    EXPECT_EQ(e.falling, 1);
}

TEST(PhysProbeTest, DetectsFallTimerReset) {
    State st;
    Sample s = physicsSample(0.f, 0.f, 0.f);
    s.fall_timer = 3.5f;
    eldenring::physprobe::commit(st, s);

    s.fall_timer = 0.f;
    const auto e = eldenring::physprobe::detectEvent(st, s);
    EXPECT_TRUE(e.fall_timer_reset);
    EXPECT_FLOAT_EQ(e.prev_fall_timer, 3.5f);
    EXPECT_FLOAT_EQ(e.fall_timer, 0.f);
}

TEST(PhysProbeTest, DetectsOriginRebaseOrTeleport) {
    State st;
    Sample s = physicsSample(0.f, 0.f, 0.f);
    eldenring::physprobe::commit(st, s);

    s.pos[0] = 8.f; // one floating-origin grid step
    const auto e = eldenring::physprobe::detectEvent(st, s);
    EXPECT_TRUE(e.position_jump);
    EXPECT_FLOAT_EQ(e.jump_distance, 8.f);

    s.pos[0] = 0.5f; // ordinary movement
    const auto e2 = eldenring::physprobe::detectEvent(st, s);
    EXPECT_FALSE(e2.position_jump);
}

TEST(PhysProbeTest, FormatsTheFieldsTheTraceNeeds) {
    Sample s = physicsSample(1.5f, 2.5f, 3.5f);
    s.now_ms = 1234;
    s.dt = 0.016f;
    s.last_pos[0] = 1.f;
    s.last_pos[1] = 2.f;
    s.last_pos[2] = 3.f;
    s.orientation[0] = 0.f;
    s.orientation[1] = 0.707f;
    s.orientation[2] = 0.f;
    s.orientation[3] = 0.707f;
    s.v120[1] = 4.f;
    s.v1b8 = 5.f;
    s.word90 = 0x0101;
    s.b91 = 0;
    s.b92 = 1;
    s.b93 = 1;
    s.b1d0 = 0;
    s.b1d1 = 1;
    s.b1d5 = 0;
    s.bcc = 2;
    s.have_vitals = true;
    s.hp = 500;
    s.max_hp = 576;
    s.fall_timer = 1.25f;
    s.have_camera = true;
    s.cam_pos[1] = 10.f;
    s.cam_fwd[2] = 1.f;
    s.fov_y = 0.8378f;

    const std::string line = eldenring::physprobe::formatSample(s);
    EXPECT_NE(line.find("t=1234"), std::string::npos);
    EXPECT_NE(line.find("pos=(1.500 2.500 3.500)"), std::string::npos);
    EXPECT_NE(line.find("last=(1.000 2.000 3.000)"), std::string::npos);
    EXPECT_NE(line.find("b90=0x0101"), std::string::npos);
    EXPECT_NE(line.find("b92=1"), std::string::npos);
    EXPECT_NE(line.find("hp=500/576"), std::string::npos);
    EXPECT_NE(line.find("fall_t=1.250"), std::string::npos);
    EXPECT_NE(line.find("fov=0.8378"), std::string::npos);
}

TEST(PhysProbeTest, FormatsEvents) {
    State st;
    Sample s = physicsSample(1.f, 2.f, 3.f);
    s.b92 = 1;
    s.b1d0 = 0;
    eldenring::physprobe::commit(st, s);

    s.now_ms = 999;
    s.b92 = 0;
    s.b1d0 = 1;
    const auto e = eldenring::physprobe::detectEvent(st, s);
    const std::string line = eldenring::physprobe::formatEvent(e, s);
    EXPECT_NE(line.find("EVENT t=999"), std::string::npos);
    EXPECT_NE(line.find("ground=1->0"), std::string::npos);
    EXPECT_NE(line.find("falling=0->1"), std::string::npos);
}
