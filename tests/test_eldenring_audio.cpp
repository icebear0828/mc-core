#include <gtest/gtest.h>

#include "eldenring_audio_data.hpp"

using namespace eldenring::audio;

namespace {

std::vector<uint8_t> makeWav(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits, size_t samples, bool junk_chunk = false) {
    std::vector<uint8_t> w;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto put16 = [&](uint16_t v) { w.push_back(static_cast<uint8_t>(v)); w.push_back(static_cast<uint8_t>(v >> 8)); };
    const size_t data_bytes = samples * channels * (bits / 8);
    w.insert(w.end(), {'R', 'I', 'F', 'F'});
    put32(static_cast<uint32_t>(36 + data_bytes + (junk_chunk ? 12 : 0)));
    w.insert(w.end(), {'W', 'A', 'V', 'E'});
    if (junk_chunk) {
        w.insert(w.end(), {'L', 'I', 'S', 'T'});
        put32(3);
        w.insert(w.end(), {1, 2, 3, 0}); // odd size + pad byte
    }
    w.insert(w.end(), {'f', 'm', 't', ' '});
    put32(16);
    put16(format);
    put16(channels);
    put32(rate);
    put32(rate * channels * (bits / 8));
    put16(static_cast<uint16_t>(channels * (bits / 8)));
    put16(bits);
    w.insert(w.end(), {'d', 'a', 't', 'a'});
    put32(static_cast<uint32_t>(data_bytes));
    w.resize(w.size() + data_bytes, 0x11);
    return w;
}

} // namespace

TEST(EldenRingAudio, ManifestLinesAreParsedAndBadOnesSkipped) {
    const std::string text =
        "entity.player.hurt|entity.player.hurt/1.wav|1.000|0.900|1.100\r\n"
        "block.stone.step|block.stone.step/2.wav|0.150|1.000|1.000\n"
        "broken line\n"
        "entity.x|a.wav|notanumber|1|1\n"
        "|empty|1|1|1\n"
        "entity.y|b.wav|0.5|1.2|0.8\n";
    const auto m = parseSoundManifest(text);
    ASSERT_EQ(m.size(), 3u);
    EXPECT_EQ(m[0].event, "entity.player.hurt");
    EXPECT_EQ(m[0].path, "entity.player.hurt/1.wav");
    EXPECT_FLOAT_EQ(m[0].pitch_min, 0.9f);
    EXPECT_FLOAT_EQ(m[0].pitch_max, 1.1f);
    EXPECT_FLOAT_EQ(m[1].volume, 0.15f);
    EXPECT_FLOAT_EQ(m[2].pitch_max, 1.2f); // an inverted range collapses to its minimum
}

TEST(EldenRingAudio, WavHeaderIsReadAndOddChunksAreSkipped) {
    const auto w = makeWav(1, 2, 44100, 16, 100);
    const WavInfo i = parseWav(w.data(), w.size());
    ASSERT_TRUE(i.ok);
    EXPECT_EQ(i.channels, 2);
    EXPECT_EQ(i.sample_rate, 44100u);
    EXPECT_EQ(i.bits, 16);
    EXPECT_EQ(i.data_size, 100u * 2u * 2u);
    EXPECT_EQ(i.data_offset, 44u);
    const auto j = makeWav(1, 1, 22050, 16, 10, true);
    const WavInfo k = parseWav(j.data(), j.size());
    ASSERT_TRUE(k.ok);
    EXPECT_EQ(k.channels, 1);
    EXPECT_EQ(k.data_size, 20u);
}

TEST(EldenRingAudio, OnlySixteenBitPcmIsAccepted) {
    EXPECT_FALSE(parseWav(makeWav(3, 1, 44100, 32, 10).data(), 44 + 40).ok); // float
    const auto eight = makeWav(1, 1, 44100, 8, 10);
    EXPECT_FALSE(parseWav(eight.data(), eight.size()).ok);
    EXPECT_FALSE(parseWav(nullptr, 0).ok);
    const uint8_t junk[16] = {};
    EXPECT_FALSE(parseWav(junk, sizeof(junk)).ok);
    const auto cut = makeWav(1, 1, 44100, 16, 10);
    EXPECT_FALSE(parseWav(cut.data(), 30).ok); // truncated before the data chunk
}

TEST(EldenRingAudio, FootstepsComeEveryStrideOnTheGroundOnly) {
    StepClock c;
    int steps = 0;
    for (int i = 0; i < 100; ++i) steps += c.update(4.f, 0.01f, true) ? 1 : 0; // 4 m in 1 s at 4 m/s
    EXPECT_EQ(steps, 2);                                                       // 4 / 1.6 = 2.5
    StepClock air;
    for (int i = 0; i < 100; ++i) EXPECT_FALSE(air.update(4.f, 0.01f, false)); // no steps in the air
    StepClock slow;
    for (int i = 0; i < 1000; ++i) EXPECT_FALSE(slow.update(0.2f, 0.01f, true)); // standing
    StepClock reset;
    reset.update(4.f, 0.3f, true);  // 1.2 m walked
    reset.update(0.f, 0.01f, true); // stops: the distance starts over
    EXPECT_FALSE(reset.update(4.f, 0.2f, true)); // 0.8 m
}
