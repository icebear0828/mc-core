#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "audio_xaudio2.hpp"
#include "eldenring_audio_data.hpp"

namespace eraudio {
namespace {

struct Clip {
    WAVEFORMATEX format{};
    std::vector<uint8_t> pcm;
    float volume{1.f};
    float pitch_min{1.f};
    float pitch_max{1.f};
};

struct Playing final : IXAudio2VoiceCallback {
    IXAudio2SourceVoice* voice{nullptr};
    std::atomic<bool> done{false};
    void STDMETHODCALLTYPE OnStreamEnd() override { done.store(true); }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

struct Request {
    std::string event;
    float volume;
};

LogFn g_log = nullptr;
IXAudio2* g_xa = nullptr;
IXAudio2MasteringVoice* g_master = nullptr;
std::map<std::string, std::vector<Clip>> g_clips;
std::vector<std::unique_ptr<Playing>> g_playing;
std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<Request> g_queue;
std::thread g_thread;
std::atomic<bool> g_stop{false};
std::atomic<float> g_master_volume{1.f};
std::atomic<bool> g_ready{false};
constexpr size_t kMaxVoices = 32;
constexpr size_t kMaxQueue = 64;

bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    const size_t got = out.empty() ? 0 : fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size() && !out.empty();
}

void Sweep() {
    for (auto it = g_playing.begin(); it != g_playing.end();) {
        if ((*it)->done.load()) {
            (*it)->voice->DestroyVoice();
            it = g_playing.erase(it);
        } else {
            ++it;
        }
    }
}

void PlayNow(const Request& r, std::mt19937& rng) {
    const auto it = g_clips.find(r.event);
    if (it == g_clips.end() || it->second.empty()) return;
    Sweep();
    if (g_playing.size() >= kMaxVoices) return;
    const Clip& clip = it->second[std::uniform_int_distribution<size_t>(0, it->second.size() - 1)(rng)];
    auto p = std::make_unique<Playing>();
    if (FAILED(g_xa->CreateSourceVoice(&p->voice, &clip.format, 0, 4.0f, p.get())) || !p->voice) return;
    const float pitch = clip.pitch_min >= clip.pitch_max ? clip.pitch_min : std::uniform_real_distribution<float>(clip.pitch_min, clip.pitch_max)(rng);
    p->voice->SetVolume(std::clamp(clip.volume * r.volume * g_master_volume.load(), 0.f, 2.f));
    p->voice->SetFrequencyRatio(std::clamp(pitch, 0.5f, 2.0f));
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = static_cast<UINT32>(clip.pcm.size());
    buf.pAudioData = clip.pcm.data();
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (FAILED(p->voice->SubmitSourceBuffer(&buf)) || FAILED(p->voice->Start(0))) {
        p->voice->DestroyVoice();
        return;
    }
    g_playing.push_back(std::move(p));
}

void Worker() {
    std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
    std::unique_lock<std::mutex> lock(g_mutex);
    while (!g_stop.load()) {
        if (g_queue.empty()) {
            g_cv.wait_for(lock, std::chrono::milliseconds(200));
            if (g_queue.empty()) {
                Sweep();
                continue;
            }
        }
        const Request r = g_queue.front();
        g_queue.pop_front();
        lock.unlock();
        PlayNow(r, rng);
        lock.lock();
    }
}

} // namespace

bool Init(const char* sounds_dir, float master_volume, LogFn log) {
    g_log = log;
    g_master_volume.store(master_volume);
    const std::string dir = sounds_dir ? sounds_dir : "";
    std::vector<uint8_t> manifest;
    if (!ReadAll(dir + "sounds_manifest.txt", manifest)) {
        if (g_log) g_log("audio: %ssounds_manifest.txt missing, no sounds (run tools/extract_mc_sounds.py)", dir.c_str());
        return false;
    }
    const auto entries = eldenring::audio::parseSoundManifest(std::string(manifest.begin(), manifest.end()));
    size_t loaded = 0;
    for (const auto& e : entries) {
        std::vector<uint8_t> bytes;
        if (!ReadAll(dir + e.path, bytes)) continue;
        const eldenring::audio::WavInfo w = eldenring::audio::parseWav(bytes.data(), bytes.size());
        if (!w.ok) continue;
        Clip c;
        c.format.wFormatTag = WAVE_FORMAT_PCM;
        c.format.nChannels = w.channels;
        c.format.nSamplesPerSec = w.sample_rate;
        c.format.wBitsPerSample = w.bits;
        c.format.nBlockAlign = static_cast<WORD>(w.channels * w.bits / 8);
        c.format.nAvgBytesPerSec = w.sample_rate * c.format.nBlockAlign;
        c.pcm.assign(bytes.begin() + static_cast<std::ptrdiff_t>(w.data_offset), bytes.begin() + static_cast<std::ptrdiff_t>(w.data_offset + w.data_size));
        c.volume = e.volume;
        c.pitch_min = e.pitch_min;
        c.pitch_max = e.pitch_max;
        g_clips[e.event].push_back(std::move(c));
        ++loaded;
    }
    if (loaded == 0) {
        if (g_log) g_log("audio: no usable sound file under %s", dir.c_str());
        return false;
    }
    // COM must be initialised on the calling thread for XAudio2Create; a different apartment already set up is fine.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)com;
    if (FAILED(XAudio2Create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !g_xa || FAILED(g_xa->CreateMasteringVoice(&g_master))) {
        if (g_log) g_log("audio: XAudio2 could not be started, no sounds");
        Shutdown();
        return false;
    }
    g_stop.store(false);
    g_thread = std::thread(Worker);
    g_ready.store(true);
    if (g_log) g_log("audio: %zu sound file(s) for %zu event(s) loaded, volume %.2f", loaded, g_clips.size(), static_cast<double>(master_volume));
    return true;
}

void Shutdown() {
    g_ready.store(false);
    g_stop.store(true);
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
    for (auto& p : g_playing) {
        if (p->voice) p->voice->DestroyVoice();
    }
    g_playing.clear();
    if (g_master) {
        g_master->DestroyVoice();
        g_master = nullptr;
    }
    if (g_xa) {
        g_xa->Release();
        g_xa = nullptr;
    }
}

void SetMasterVolume(float volume) { g_master_volume.store(volume); }

void Play(const char* event, float volume) {
    if (!g_ready.load(std::memory_order_relaxed) || !event) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_queue.size() >= kMaxQueue) return;
        g_queue.push_back({event, volume});
    }
    g_cv.notify_one();
}

} // namespace eraudio
