#include "mc/collision_field.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace mc {
namespace {

constexpr float kSectionSize = static_cast<float>(CollisionField::kSectionBlocks);
constexpr float kSubBlock = CollisionField::kSubBlockSize;
constexpr float kEpsilon = 1e-6f;

float component(const Vec3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

void setComponent(Vec3& v, int axis, float value) {
    if (axis == 0) {
        v.x = value;
    } else if (axis == 1) {
        v.y = value;
    } else {
        v.z = value;
    }
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float floorToSubBlock(float v) {
    return std::floor(v / kSubBlock) * kSubBlock;
}

float ceilToSubBlock(float v) {
    return std::ceil(v / kSubBlock) * kSubBlock;
}

// A box has volume only when every axis is positive; NaN compares false and is rejected too.
bool isEmpty(const Aabb& box) {
    return !(box.min.x < box.max.x && box.min.y < box.max.y && box.min.z < box.max.z);
}

// Positive-volume overlap: boxes that only share a face do not count.
bool overlapsStrict(const Aabb& a, const Aabb& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

Aabb sectionBounds(const SectionKey& key) {
    const float x0 = static_cast<float>(key.x) * kSectionSize;
    const float y0 = static_cast<float>(key.y) * kSectionSize;
    const float z0 = static_cast<float>(key.z) * kSectionSize;
    return Aabb{{x0, y0, z0}, {x0 + kSectionSize, y0 + kSectionSize, z0 + kSectionSize}};
}

// Section keys from lo to hi inclusive, ascending by (x, y, z) so every traversal is
// deterministic. Also usable for a zero-volume range, which an axis-aligned ray produces.
std::vector<SectionKey> keysBetween(const SectionKey& lo, const SectionKey& hi) {
    std::vector<SectionKey> keys;
    for (int32_t x = lo.x; x <= hi.x; ++x) {
        for (int32_t y = lo.y; y <= hi.y; ++y) {
            for (int32_t z = lo.z; z <= hi.z; ++z) {
                keys.push_back(SectionKey{x, y, z});
            }
        }
    }
    std::sort(keys.begin(), keys.end(), [](const SectionKey& a, const SectionKey& b) {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.z < b.z;
    });
    return keys;
}

// Section keys covering a box.
std::vector<SectionKey> keysOverlapping(const Aabb& box) {
    if (isEmpty(box)) {
        return {};
    }
    return keysBetween(CollisionField::sectionKeyFor(box.min), CollisionField::sectionKeyFor(box.max));
}

float distanceSqToSection(const SectionKey& key, const Vec3& point) {
    const Aabb bounds = sectionBounds(key);
    float distance_sq = 0.f;
    for (int axis = 0; axis < 3; ++axis) {
        const float p = component(point, axis);
        const float lo = component(bounds.min, axis);
        const float hi = component(bounds.max, axis);
        const float d = p < lo ? lo - p : (p > hi ? p - hi : 0.f);
        distance_sq += d * d;
    }
    return distance_sq;
}

// Slab test. t_enter is clamped to 0, so a ray starting inside a box hits at distance 0 with the
// normal facing back along the ray.
bool intersectBox(const Aabb& box, const Vec3& origin, const Vec3& direction, float max_distance,
                  float& out_distance, Vec3& out_normal) {
    float t_enter = 0.f;
    float t_exit = max_distance;
    int enter_axis = -1;
    float enter_sign = 0.f;

    for (int axis = 0; axis < 3; ++axis) {
        const float o = component(origin, axis);
        const float d = component(direction, axis);
        const float lo = component(box.min, axis);
        const float hi = component(box.max, axis);
        if (std::abs(d) < kEpsilon) {
            if (o < lo || o > hi) {
                return false; // parallel to this slab and outside it
            }
            continue;
        }
        float t_near = (lo - o) / d;
        float t_far = (hi - o) / d;
        float sign = -1.f; // entering through the low face
        if (t_near > t_far) {
            std::swap(t_near, t_far);
            sign = 1.f; // entering through the high face
        }
        if (t_near > t_enter) {
            t_enter = t_near;
            enter_axis = axis;
            enter_sign = sign;
        }
        if (t_far < t_exit) {
            t_exit = t_far;
        }
        if (t_enter > t_exit) {
            return false;
        }
    }
    if (t_enter > max_distance) {
        return false;
    }
    out_distance = t_enter;
    if (enter_axis < 0) {
        out_normal = direction * -1.f; // started inside the box
    } else {
        Vec3 normal{};
        setComponent(normal, enter_axis, enter_sign);
        out_normal = normal;
    }
    return true;
}

} // namespace

SectionKey CollisionField::sectionKeyFor(const Vec3& point) {
    return SectionKey{
        static_cast<int32_t>(std::floor(point.x / kSectionSize)),
        static_cast<int32_t>(std::floor(point.y / kSectionSize)),
        static_cast<int32_t>(std::floor(point.z / kSectionSize))};
}

void CollisionField::addAabb(const Aabb& box) {
    if (isEmpty(box)) {
        return; // degenerate input carries no volume
    }
    Aabb snapped;
    snapped.min = {floorToSubBlock(box.min.x), floorToSubBlock(box.min.y), floorToSubBlock(box.min.z)};
    snapped.max = {ceilToSubBlock(box.max.x), ceilToSubBlock(box.max.y), ceilToSubBlock(box.max.z)};
    if (isEmpty(snapped)) {
        return;
    }
    insertSnapped(snapped);
}

void CollisionField::insertSnapped(const Aabb& box) {
    for (const SectionKey& key : keysOverlapping(box)) {
        const Aabb bounds = sectionBounds(key);
        Aabb piece;
        piece.min = {std::max(box.min.x, bounds.min.x), std::max(box.min.y, bounds.min.y),
                     std::max(box.min.z, bounds.min.z)};
        piece.max = {std::min(box.max.x, bounds.max.x), std::min(box.max.y, bounds.max.y),
                     std::min(box.max.z, bounds.max.z)};
        if (isEmpty(piece)) {
            continue;
        }
        Section& section = sections_[key];
        section.boxes.push_back(piece);
        ++section.version;
    }
}

void CollisionField::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 raw_normal = cross(b - a, c - a);
    if (raw_normal.lengthSq() < 1e-12f) {
        return; // degenerate triangle
    }
    Vec3 normal = raw_normal.normalized();
    if (normal.y < 0.f) {
        normal = normal * -1.f; // orient upward so vertex winding does not matter
    }

    Aabb bounds;
    bounds.min = {std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})};
    bounds.max = {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})};

    if (normal.y >= kWalkableNormalY) {
        // Walkable: the surface becomes sub-block micro-steps. A flat triangle has no vertical
        // extent, so it is thickened downward to keep the walkable face where the geometry is.
        Aabb step_box = bounds;
        if (step_box.max.y - step_box.min.y < kSubBlock) {
            step_box.min.y = step_box.max.y - kSubBlock;
        }
        addAabb(step_box);
        return;
    }
    addWallColumns(bounds);
}

void CollisionField::addWallColumns(const Aabb& bounds) {
    Aabb footprint = bounds;
    // A vertical triangle has no thickness on one horizontal axis; give it one sub-block so the
    // column still collides.
    if (footprint.max.x - footprint.min.x < kSubBlock) {
        footprint.max.x = footprint.min.x + kSubBlock;
    }
    if (footprint.max.z - footprint.min.z < kSubBlock) {
        footprint.max.z = footprint.min.z + kSubBlock;
    }

    for (const SectionKey& key : keysOverlapping(footprint)) {
        const Aabb bounds_of_section = sectionBounds(key);
        Aabb column;
        column.min = {std::max(footprint.min.x, bounds_of_section.min.x), bounds_of_section.min.y,
                      std::max(footprint.min.z, bounds_of_section.min.z)};
        column.max = {std::min(footprint.max.x, bounds_of_section.max.x), bounds_of_section.max.y,
                      std::min(footprint.max.z, bounds_of_section.max.z)};
        if (isEmpty(column)) {
            continue;
        }
        Aabb snapped;
        snapped.min = {floorToSubBlock(column.min.x), floorToSubBlock(column.min.y),
                       floorToSubBlock(column.min.z)};
        snapped.max = {ceilToSubBlock(column.max.x), ceilToSubBlock(column.max.y),
                       ceilToSubBlock(column.max.z)};
        if (isEmpty(snapped)) {
            continue;
        }
        insertSnapped(snapped);
    }
}

void CollisionField::clearSection(const SectionKey& key) {
    auto it = sections_.find(key);
    if (it == sections_.end() || it->second.boxes.empty()) {
        return;
    }
    it->second.boxes.clear();
    ++it->second.version; // keep the entry so delta consumers still see the clear
}

void CollisionField::evictOutside(const Vec3& center, float radius) {
    const float r = std::max(radius, 0.f);
    const float r_sq = r * r;
    for (auto it = sections_.begin(); it != sections_.end();) {
        if (distanceSqToSection(it->first, center) > r_sq) {
            it = sections_.erase(it);
        } else {
            ++it;
        }
    }
}

uint32_t CollisionField::sectionVersion(const SectionKey& key) const {
    const auto it = sections_.find(key);
    return it == sections_.end() ? 0u : it->second.version;
}

std::vector<Aabb> CollisionField::collect(const Aabb& query) const {
    std::vector<Aabb> result;
    if (isEmpty(query)) {
        return result;
    }
    for (const SectionKey& key : keysOverlapping(query)) {
        const auto it = sections_.find(key);
        if (it == sections_.end()) {
            continue;
        }
        for (const Aabb& box : it->second.boxes) {
            if (overlapsStrict(box, query)) {
                result.push_back(box);
            }
        }
    }
    return result;
}

bool CollisionField::overlaps(const Aabb& query) const {
    if (isEmpty(query)) {
        return false;
    }
    for (const SectionKey& key : keysOverlapping(query)) {
        const auto it = sections_.find(key);
        if (it == sections_.end()) {
            continue;
        }
        for (const Aabb& box : it->second.boxes) {
            if (overlapsStrict(box, query)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<CollisionRayHit> CollisionField::raycast(const Vec3& origin, const Vec3& direction,
                                                       float max_distance) const {
    if (!(max_distance > 0.f)) {
        return std::nullopt;
    }
    const Vec3 dir = direction.normalized();
    if (dir.lengthSq() < 0.5f) {
        return std::nullopt; // zero-length direction
    }

    const Vec3 end = origin + dir * max_distance;
    const Aabb query{{std::min(origin.x, end.x), std::min(origin.y, end.y), std::min(origin.z, end.z)},
                     {std::max(origin.x, end.x), std::max(origin.y, end.y), std::max(origin.z, end.z)}};
    // An axis-aligned ray spans no volume, so the range is taken from the endpoints directly.
    const std::vector<SectionKey> keys =
        keysBetween(sectionKeyFor(query.min), sectionKeyFor(query.max));

    bool found = false;
    float best_distance = max_distance;
    CollisionRayHit best;

    for (const SectionKey& key : keys) {
        const auto it = sections_.find(key);
        if (it == sections_.end()) {
            continue;
        }
        for (const Aabb& box : it->second.boxes) {
            float distance = 0.f;
            Vec3 normal{};
            if (!intersectBox(box, origin, dir, best_distance, distance, normal)) {
                continue;
            }
            if (found && !(distance < best_distance)) {
                continue; // keep the first hit on a tie
            }
            found = true;
            best_distance = distance;
            best.point = origin + dir * distance;
            best.normal = normal;
            best.distance = distance;
        }
    }

    if (!found) {
        return std::nullopt;
    }
    return best;
}

size_t CollisionField::sectionCount() const {
    size_t count = 0;
    for (const auto& entry : sections_) {
        if (!entry.second.boxes.empty()) {
            ++count;
        }
    }
    return count;
}

size_t CollisionField::boxCount() const {
    size_t count = 0;
    for (const auto& entry : sections_) {
        count += entry.second.boxes.size();
    }
    return count;
}

} // namespace mc
