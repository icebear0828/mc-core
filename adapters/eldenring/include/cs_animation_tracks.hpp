#pragma once

#include <algorithm>
#include <cmath>
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

struct Quat4 {
    float x{0.f}, y{0.f}, z{0.f}, w{1.f};
};

// The animation tracks are rigid body transforms. Interpolating the 16 matrix
// entries directly shears the rotation basis between keyframes, which shows up
// as a few "wind-blown" frames during fast reload/inspect animations. Decompose
// into translation + quaternion + scale and slerp the rotation instead.
inline Quat4 quatFromBasis(float r00, float r01, float r02,
                           float r10, float r11, float r12,
                           float r20, float r21, float r22) {
    Quat4 q;
    const float trace = r00 + r11 + r22;
    if (trace > 0.f) {
        const float s = std::sqrt(trace + 1.f) * 2.f;
        q.w = 0.25f * s;
        q.x = (r21 - r12) / s;
        q.y = (r02 - r20) / s;
        q.z = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.f + r00 - r11 - r22) * 2.f;
        q.w = (r21 - r12) / s;
        q.x = 0.25f * s;
        q.y = (r01 + r10) / s;
        q.z = (r02 + r20) / s;
    } else if (r11 > r22) {
        const float s = std::sqrt(1.f + r11 - r00 - r22) * 2.f;
        q.w = (r02 - r20) / s;
        q.x = (r01 + r10) / s;
        q.y = 0.25f * s;
        q.z = (r12 + r21) / s;
    } else {
        const float s = std::sqrt(1.f + r22 - r00 - r11) * 2.f;
        q.w = (r10 - r01) / s;
        q.x = (r02 + r20) / s;
        q.y = (r12 + r21) / s;
        q.z = 0.25f * s;
    }
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 1e-8f) {
        q.x /= len; q.y /= len; q.z /= len; q.w /= len;
    } else {
        q = Quat4{};
    }
    return q;
}

inline Quat4 slerp(const Quat4& a, const Quat4& b, float t) {
    Quat4 q1 = b;
    float dot = a.x * q1.x + a.y * q1.y + a.z * q1.z + a.w * q1.w;
    if (dot < 0.f) {
        q1.x = -q1.x; q1.y = -q1.y; q1.z = -q1.z; q1.w = -q1.w;
        dot = -dot;
    }
    if (dot > 0.9995f) {
        Quat4 r;
        r.x = a.x + (q1.x - a.x) * t;
        r.y = a.y + (q1.y - a.y) * t;
        r.z = a.z + (q1.z - a.z) * t;
        r.w = a.w + (q1.w - a.w) * t;
        const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (len > 1e-8f) { r.x /= len; r.y /= len; r.z /= len; r.w /= len; }
        return r;
    }
    const float theta = std::acos(std::clamp(dot, -1.f, 1.f));
    const float sin_theta = std::sin(theta);
    const float w1 = std::sin((1.f - t) * theta) / sin_theta;
    const float w2 = std::sin(t * theta) / sin_theta;
    return Quat4{
        a.x * w1 + q1.x * w2,
        a.y * w1 + q1.y * w2,
        a.z * w1 + q1.z * w2,
        a.w * w1 + q1.w * w2,
    };
}

inline eldenring::fp::M4 LerpRigidM4(const eldenring::fp::M4& a, const eldenring::fp::M4& b, float t) {
    auto finite = [](const eldenring::fp::M4& m) {
        for (float v : m.m) {
            if (!std::isfinite(v)) return false;
        }
        return true;
    };
    // A corrupted source frame must never poison the render constants.
    if (!finite(a) || !finite(b)) {
        if (finite(a)) return a;
        if (finite(b)) return b;
        return eldenring::fp::M4{};
    }

    // Columns of the 3x3 basis, in the M4 row-major / column-vector layout.
    float ax = a.m[0], ay = a.m[4], az = a.m[8];
    float bx = a.m[1], by = a.m[5], bz = a.m[9];
    float cx = a.m[2], cy = a.m[6], cz = a.m[10];
    float dx = b.m[0], dy = b.m[4], dz = b.m[8];
    float ex = b.m[1], ey = b.m[5], ez = b.m[9];
    float fx = b.m[2], fy = b.m[6], fz = b.m[10];

    const float sa0 = std::sqrt(ax * ax + ay * ay + az * az);
    const float sa1 = std::sqrt(bx * bx + by * by + bz * bz);
    const float sa2 = std::sqrt(cx * cx + cy * cy + cz * cz);
    const float sb0 = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float sb1 = std::sqrt(ex * ex + ey * ey + ez * ez);
    const float sb2 = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (sa0 < 1e-6f || sa1 < 1e-6f || sa2 < 1e-6f ||
        sb0 < 1e-6f || sb1 < 1e-6f || sb2 < 1e-6f) {
        return LerpM4(a, b, t);
    }

    const Quat4 qa = quatFromBasis(ax / sa0, bx / sa1, cx / sa2,
                                   ay / sa0, by / sa1, cy / sa2,
                                   az / sa0, bz / sa1, cz / sa2);
    const Quat4 qb = quatFromBasis(dx / sb0, ex / sb1, fx / sb2,
                                   dy / sb0, ey / sb1, fy / sb2,
                                   dz / sb0, ez / sb1, fz / sb2);
    const Quat4 q = slerp(qa, qb, t);

    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    const float sx = sa0 + (sb0 - sa0) * t;
    const float sy = sa1 + (sb1 - sa1) * t;
    const float sz = sa2 + (sb2 - sa2) * t;

    eldenring::fp::M4 r;
    r.m[0] = (1.f - 2.f * (yy + zz)) * sx;
    r.m[4] = (2.f * (xy + wz)) * sx;
    r.m[8] = (2.f * (xz - wy)) * sx;
    r.m[1] = (2.f * (xy - wz)) * sy;
    r.m[5] = (1.f - 2.f * (xx + zz)) * sy;
    r.m[9] = (2.f * (yz + wx)) * sy;
    r.m[2] = (2.f * (xz + wy)) * sz;
    r.m[6] = (2.f * (yz - wx)) * sz;
    r.m[10] = (1.f - 2.f * (xx + yy)) * sz;
    r.m[3] = a.m[3] + (b.m[3] - a.m[3]) * t;
    r.m[7] = a.m[7] + (b.m[7] - a.m[7]) * t;
    r.m[11] = a.m[11] + (b.m[11] - a.m[11]) * t;
    r.m[12] = 0.f; r.m[13] = 0.f; r.m[14] = 0.f; r.m[15] = 1.f;
    if (!finite(r)) return a;
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
    out = LerpRigidM4(track[f0], track[f1], alpha);
}

inline void SampleDynamicTrack(const std::vector<eldenring::fp::M4>& track, float fps, float anim_time, eldenring::fp::M4& out) {
    if (track.empty()) return;
    SampleTrack(track.data(), track.size(), fps, anim_time, out);
}

} // namespace erov::cs_anims
