#pragma once

#include <vector>
#include <cstddef>
#include "eldenring_fp.hpp"

namespace erov::cs_anims {

struct KeyframePartPose {
    eldenring::fp::M4 blade;
    eldenring::fp::M4 front;
    eldenring::fp::M4 rear;
    eldenring::fp::M4 lock;
    eldenring::fp::M4 arms;
};

inline eldenring::fp::M4 LerpM4(const eldenring::fp::M4& a, const eldenring::fp::M4& b, float t) {
    eldenring::fp::M4 r;
    for (size_t i = 0; i < 16; ++i) {
        r.m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;
    }
    return r;
}

inline void SampleTrack(const eldenring::fp::M4* track, size_t count, float fps, float anim_time, eldenring::fp::M4& out) {
    if (count == 0 || track == nullptr) return;
    if (count == 1) { out = track[0]; return; }

    const float frame_float = anim_time * fps;
    const size_t f0 = static_cast<size_t>(frame_float);
    if (f0 >= count - 1) {
        out = track[count - 1];
        return;
    }
    const size_t f1 = f0 + 1;
    const float alpha = frame_float - static_cast<float>(f0);
    out = LerpM4(track[f0], track[f1], alpha);
}

inline void SampleDynamicTrack(const std::vector<eldenring::fp::M4>& track, float fps, float anim_time, eldenring::fp::M4& out) {
    if (track.empty()) return;
    SampleTrack(track.data(), track.size(), fps, anim_time, out);
}

} // namespace erov::cs_anims
