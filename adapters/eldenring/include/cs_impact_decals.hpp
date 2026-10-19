#pragma once

// Impact decals for CS hitscan bullets on static geometry (floors, walls, rocks). Pure logic: normal
// orientation, tangent basis, fade curve and a bounded ring. CombatService publishes the world position and
// normal; the D3D12 overlay owns the drawing. No game memory or Windows headers, so the whole slice is
// unit-tested without the game.
//
// Only static geometry gets a decal. Characters are skipped: a hole on a moving enemy would need bone
// attachment, and the impact report can arrive a frame after the shot. This matches the field note's
// "bullet holes in the air / on enemies" gotcha: require a map ray hit and skip character impacts.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eldenring::cs {

inline constexpr size_t kMaxImpactDecals = 128;
inline constexpr float kImpactDecalLifeS = 20.f;
// Two hits closer than this refresh one decal instead of stacking a new one on the same spot.
inline constexpr float kImpactDecalMinSeparationM = 0.03f;
// The last 20% of the lifetime fades out.
inline constexpr float kImpactDecalFadeStart = 0.8f;

struct ImpactDecal {
    float pos[3]{0.f, 0.f, 0.f};
    float normal[3]{0.f, 1.f, 0.f};
    uint64_t start_ms{0};
    float life_s{kImpactDecalLifeS};
};

// The surface normal from the Havok ray can point into the wall. Flip it so the decal faces the shooter.
inline void orientDecalNormal(const float in[3], const float shot_dir[3], float out[3]) {
    const float len = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    if (!(len > 1e-6f)) {
        out[0] = 0.f;
        out[1] = 1.f;
        out[2] = 0.f;
        return;
    }
    for (int i = 0; i < 3; ++i) out[i] = in[i] / len;
    const float facing = out[0] * shot_dir[0] + out[1] * shot_dir[1] + out[2] * shot_dir[2];
    if (facing > 0.f) {
        for (int i = 0; i < 3; ++i) out[i] = -out[i];
    }
}

// Orthonormal basis on the decal plane. `normal` must be unit length. Picks the world axis least aligned
// with the normal, so a floor (0,1,0) and a wall (1,0,0) both get a stable basis.
inline void decalTangentBasis(const float normal[3], float tangent[3], float bitangent[3]) {
    float helper[3] = {0.f, 1.f, 0.f};
    if (std::fabs(normal[1]) > 0.9f) {
        helper[0] = 1.f;
        helper[1] = 0.f;
        helper[2] = 0.f;
    }
    tangent[0] = helper[1] * normal[2] - helper[2] * normal[1];
    tangent[1] = helper[2] * normal[0] - helper[0] * normal[2];
    tangent[2] = helper[0] * normal[1] - helper[1] * normal[0];
    const float t_len = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
    if (t_len > 1e-6f) {
        for (int i = 0; i < 3; ++i) tangent[i] /= t_len;
    } else {
        tangent[0] = 1.f;
        tangent[1] = 0.f;
        tangent[2] = 0.f;
    }
    bitangent[0] = normal[1] * tangent[2] - normal[2] * tangent[1];
    bitangent[1] = normal[2] * tangent[0] - normal[0] * tangent[2];
    bitangent[2] = normal[0] * tangent[1] - normal[1] * tangent[0];
}

// 1 while the decal is fresh, then linearly down to 0 at the end of its life.
inline float decalFade(float age_s, float life_s) {
    if (!(life_s > 0.f)) return 0.f;
    const float u = age_s / life_s;
    if (u <= kImpactDecalFadeStart) return 1.f;
    return std::clamp((1.f - u) / (1.f - kImpactDecalFadeStart), 0.f, 1.f);
}

// Fixed-capacity decal list. A hit within kImpactDecalMinSeparationM refreshes the existing decal instead of
// adding another; a full list evicts the oldest entry. The caller owns the lock.
class ImpactDecalRing {
public:
    bool addOrRefresh(const float pos[3], const float normal[3], uint64_t now_ms,
                      float life_s = kImpactDecalLifeS) {
        constexpr float sep2 = kImpactDecalMinSeparationM * kImpactDecalMinSeparationM;
        for (size_t i = 0; i < count_; ++i) {
            if (distanceSquared(items_[i].pos, pos) <= sep2) {
                copy3(normal, items_[i].normal);
                items_[i].start_ms = now_ms;
                items_[i].life_s = life_s;
                return false;
            }
        }
        if (count_ < kMaxImpactDecals) {
            init(items_[count_], pos, normal, now_ms, life_s);
            ++count_;
            return true;
        }
        size_t oldest = 0;
        for (size_t i = 1; i < count_; ++i) {
            if (items_[i].start_ms < items_[oldest].start_ms) oldest = i;
        }
        init(items_[oldest], pos, normal, now_ms, life_s);
        return true;
    }

    void expire(uint64_t now_ms) {
        for (size_t i = 0; i < count_;) {
            const float age = static_cast<float>(now_ms - items_[i].start_ms) / 1000.f;
            if (age >= items_[i].life_s) {
                items_[i] = items_[--count_];
            } else {
                ++i;
            }
        }
    }

    [[nodiscard]] size_t size() const { return count_; }
    [[nodiscard]] const ImpactDecal& at(size_t i) const { return items_[i]; }

private:
    static void copy3(const float in[3], float out[3]) {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
    }
    static float distanceSquared(const float a[3], const float b[3]) {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz;
    }
    static void init(ImpactDecal& d, const float pos[3], const float normal[3], uint64_t now_ms, float life_s) {
        copy3(pos, d.pos);
        copy3(normal, d.normal);
        d.start_ms = now_ms;
        d.life_s = life_s;
    }

    ImpactDecal items_[kMaxImpactDecals]{};
    size_t count_{0};
};

} // namespace eldenring::cs
