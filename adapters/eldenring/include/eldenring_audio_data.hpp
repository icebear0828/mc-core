#pragma once

// The platform-independent parts of the sound player: the manifest the extractor writes (tools/extract_mc_sounds.py), the
// WAV header, and the footstep clock. The XAudio2 playback itself is Windows-only (src/audio_xaudio2.cpp).

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace eldenring::audio {

struct SoundEntry {
    std::string event;
    std::string path; // relative to the sounds folder
    float volume{1.f};
    float pitch_min{1.f};
    float pitch_max{1.f};
};

// One line per file: `event|relative path|volume|pitch_min|pitch_max`. Malformed lines are skipped.
inline std::vector<SoundEntry> parseSoundManifest(const std::string& text) {
    std::vector<SoundEntry> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        size_t start = 0;
        for (;;) {
            const size_t bar = line.find('|', start);
            f.push_back(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        if (f.size() != 5 || f[0].empty() || f[1].empty()) continue;
        SoundEntry e;
        e.event = f[0];
        e.path = f[1];
        try {
            e.volume = std::stof(f[2]);
            e.pitch_min = std::stof(f[3]);
            e.pitch_max = std::stof(f[4]);
        } catch (...) {
            continue;
        }
        if (e.pitch_max < e.pitch_min) e.pitch_max = e.pitch_min;
        out.push_back(std::move(e));
    }
    return out;
}

struct WavInfo {
    bool ok{false};
    uint16_t channels{0};
    uint32_t sample_rate{0};
    uint16_t bits{0};
    size_t data_offset{0};
    size_t data_size{0};
};

// Reads a PCM WAV (RIFF/WAVE, `fmt ` format 1, `data` chunk). Anything else is not ok.
inline WavInfo parseWav(const uint8_t* b, size_t n) {
    WavInfo w;
    if (!b || n < 12 || std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0) return w;
    auto u16 = [&](size_t o) { return static_cast<uint16_t>(b[o] | (b[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) | (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24); };
    bool have_fmt = false;
    size_t pos = 12;
    while (pos + 8 <= n) {
        const uint32_t size = u32(pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(b + pos, "fmt ", 4) == 0 && size >= 16 && body + 16 <= n) {
            if (u16(body) != 1) return WavInfo{}; // not PCM
            w.channels = u16(body + 2);
            w.sample_rate = u32(body + 4);
            w.bits = u16(body + 14);
            have_fmt = true;
        } else if (std::memcmp(b + pos, "data", 4) == 0) {
            if (!have_fmt || body > n) return WavInfo{};
            w.data_offset = body;
            w.data_size = std::min<size_t>(size, n - body);
            w.ok = w.channels >= 1 && w.channels <= 2 && w.bits == 16 && w.sample_rate >= 8000 && w.data_size > 0;
            return w;
        }
        pos = body + size + (size & 1); // chunks are word aligned
    }
    return WavInfo{};
}

// Footsteps: one every `stride` metres walked on the ground (Minecraft plays one roughly every 1.6 m at walking speed).
class StepClock {
public:
    static constexpr float kStrideM = 1.6f;
    static constexpr float kMinSpeed = 0.5f; // below this the character is standing

    bool update(float horizontal_speed_mps, float dt, bool on_ground) {
        if (!on_ground || horizontal_speed_mps < kMinSpeed) {
            walked_ = 0.f;
            return false;
        }
        walked_ += horizontal_speed_mps * dt;
        if (walked_ >= kStrideM) {
            walked_ -= kStrideM;
            return true;
        }
        return false;
    }

private:
    float walked_{0.f};
};

} // namespace eldenring::audio
