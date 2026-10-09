#pragma once

// Plays the extracted Minecraft sounds (mods\mc_adapter\sounds, see tools/extract_mc_sounds.py) with XAudio2. Play() only
// queues the event name, a worker thread does the XAudio2 calls, so the game thread is never held up.

namespace eraudio {

using LogFn = void (*)(const char* fmt, ...);

// `sounds_dir` ends with a path separator. Returns false (and plays nothing later) when XAudio2 or the files are missing.
bool Init(const char* sounds_dir, float master_volume, LogFn log);
void Shutdown();
void SetMasterVolume(float volume);

// Thread safe, cheap. Unknown events are ignored. `volume` scales the file's own volume.
void Play(const char* event, float volume = 1.f);

} // namespace eraudio
