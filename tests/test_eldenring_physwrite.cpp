#include <gtest/gtest.h>

#include "eldenring_physwrite.hpp"

TEST(PhysWriteTest, OnlyFourHeightsAreValid) {
    EXPECT_TRUE(eldenring::physwrite::validIndex(0));
    EXPECT_TRUE(eldenring::physwrite::validIndex(3));
    EXPECT_FALSE(eldenring::physwrite::validIndex(-1));
    EXPECT_FALSE(eldenring::physwrite::validIndex(4));
}

TEST(PhysWriteTest, HeightsAreOneFiveTenTwentyMetres) {
    EXPECT_FLOAT_EQ(eldenring::physwrite::kHeightsM[0], 1.f);
    EXPECT_FLOAT_EQ(eldenring::physwrite::kHeightsM[1], 5.f);
    EXPECT_FLOAT_EQ(eldenring::physwrite::kHeightsM[2], 10.f);
    EXPECT_FLOAT_EQ(eldenring::physwrite::kHeightsM[3], 20.f);
}

TEST(PhysWriteTest, FormatsRequests) {
    const std::string line = eldenring::physwrite::formatRequest(1);
    EXPECT_NE(line.find("+5 m"), std::string::npos);
    EXPECT_NE(line.find("Numpad1"), std::string::npos);
}

TEST(PhysWriteTest, FormatsTheWriteResult) {
    const float before[3] = {1.f, 2.f, 3.f};
    const float after[3] = {1.f, 7.f, 3.f};
    const float read_back[3] = {1.f, 7.f, 3.f};
    const std::string line = eldenring::physwrite::formatResult(1, before, after, read_back, true, true, 1, 0, 0.25f, 500, 576);
    EXPECT_NE(line.find("key=1"), std::string::npos);
    EXPECT_NE(line.find("delta=+5"), std::string::npos);
    EXPECT_NE(line.find("before=(1.000 2.000 3.000)"), std::string::npos);
    EXPECT_NE(line.find("after=(1.000 7.000 3.000)"), std::string::npos);
    EXPECT_NE(line.find("read=(1.000 7.000 3.000)"), std::string::npos);
    EXPECT_NE(line.find("ground=1"), std::string::npos);
    EXPECT_NE(line.find("fall_t=0.250"), std::string::npos);
    EXPECT_NE(line.find("hp=500/576"), std::string::npos);
}

TEST(PhysWriteTest, HoldKeepsTheTargetForTheRequestedTime) {
    eldenring::physwrite::HoldState h;
    const float pos[3] = {1.f, 2.f, 3.f};
    eldenring::physwrite::startHold(h, pos, 5.f, 1000, 500);
    EXPECT_TRUE(h.active);
    EXPECT_FLOAT_EQ(h.target[0], 1.f);
    EXPECT_FLOAT_EQ(h.target[1], 7.f);
    EXPECT_FLOAT_EQ(h.target[2], 3.f);
    EXPECT_TRUE(eldenring::physwrite::holdActive(h, 1200));
    EXPECT_FALSE(eldenring::physwrite::holdActive(h, 1500));
}

TEST(PhysWriteTest, ZeroHoldIsAOneShot) {
    eldenring::physwrite::HoldState h;
    const float pos[3] = {0.f, 0.f, 0.f};
    eldenring::physwrite::startHold(h, pos, 1.f, 1000, 0);
    EXPECT_FALSE(h.active);
    EXPECT_FALSE(eldenring::physwrite::holdActive(h, 1000));
}

TEST(PhysWriteTest, StopHoldClearsIt) {
    eldenring::physwrite::HoldState h;
    const float pos[3] = {0.f, 0.f, 0.f};
    eldenring::physwrite::startHold(h, pos, 1.f, 1000, 500);
    eldenring::physwrite::stopHold(h);
    EXPECT_FALSE(h.active);
    EXPECT_FALSE(eldenring::physwrite::holdActive(h, 1100));
}

TEST(PhysWriteTest, GlideMovesTheTargetHorizontallyAtTheConfiguredSpeed) {
    eldenring::physwrite::HoldState h;
    const float pos[3] = {1.f, 2.f, 3.f};
    eldenring::physwrite::startHold(h, pos, 5.f, 1000, 5000);
    h.glide = true;
    h.speed_mps = 10.f;
    const float forward[3] = {0.f, -0.5f, 1.f}; // vertical component is ignored
    eldenring::physwrite::stepGlide(h, forward, 2000); // 1 s
    EXPECT_NEAR(h.target[0], 1.f, 1e-4f);
    EXPECT_NEAR(h.target[1], 7.f, 1e-4f);
    EXPECT_NEAR(h.target[2], 13.f, 1e-4f);
}

TEST(PhysWriteTest, GlideWithoutHorizontalForwardDoesNotMove) {
    eldenring::physwrite::HoldState h;
    const float pos[3] = {0.f, 0.f, 0.f};
    eldenring::physwrite::startHold(h, pos, 5.f, 1000, 5000);
    h.glide = true;
    h.speed_mps = 10.f;
    const float forward[3] = {0.f, -1.f, 0.f};
    eldenring::physwrite::stepGlide(h, forward, 2000);
    EXPECT_FLOAT_EQ(h.target[0], 0.f);
    EXPECT_FLOAT_EQ(h.target[2], 0.f);
}
