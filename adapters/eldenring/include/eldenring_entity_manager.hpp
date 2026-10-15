#pragma once

#include "eldenring_world.hpp"
#include "eldenring_pick.hpp"
#include "mc/targeting.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace eldenring::live {

class EntityManager {
public:
    static EntityManager& instance() {
        static EntityManager s_instance;
        return s_instance;
    }

    void updateCacheFromGameThread(std::vector<EnemyInfo> enemies) {
        // Sort enemies by 3D distance to player so nearest entities are always first
        std::sort(enemies.begin(), enemies.end(), [](const EnemyInfo& a, const EnemyInfo& b) noexcept {
            const float da = a.rel_x * a.rel_x + a.rel_y * a.rel_y + a.rel_z * a.rel_z;
            const float db = b.rel_x * b.rel_x + b.rel_y * b.rel_y + b.rel_z * b.rel_z;
            return da < db;
        });
        std::lock_guard<std::mutex> g(mutex_);
        cached_enemies_ = std::move(enemies);
    }

    [[nodiscard]] std::vector<EnemyInfo> getCachedEnemies() const {
        std::lock_guard<std::mutex> g(mutex_);
        return cached_enemies_;
    }

    void clearCache() {
        std::lock_guard<std::mutex> g(mutex_);
        cached_enemies_.clear();
    }

    struct PickResult {
        int index{-1};
        std::vector<EnemyInfo> list;
        bool was_fallback{false};
    };

    // Finds the best target under crosshair. Tries cached enemies first;
    // on miss, checks air-swing throttle and falls back to full enumeration (up to 2000 enemies).
    PickResult pickTargetWithFallback(const float cam_rel[3], const float dir[3],
                                      const IMemoryReader& reader, uintptr_t image_base,
                                      uint64_t now_ms, const float player_feet[3],
                                      const PickParams& p = {}) {
        PickResult res;
        {
            std::lock_guard<std::mutex> g(mutex_);
            res.list = cached_enemies_;
        }

        if (!res.list.empty()) {
            res.index = pickTarget(cam_rel, dir, res.list, p);
            if (res.index >= 0) return res;
        }

        // Cached list had no target. Check air-swing throttle.
        const mc::Vec3 current_pos{player_feet[0], player_feet[1], player_feet[2]};
        if (throttle_.shouldThrottle(now_ms, current_pos, 200, 0.5f)) {
            // Air swing throttled: return miss without hitting game memory
            return res;
        }

        // Full enumeration fallback
        std::vector<EnemyInfo> full_list;
        if (enumerateEnemies(reader, image_base, full_list, 2000)) {
            // Sort full list by distance so nearest are at front
            std::sort(full_list.begin(), full_list.end(), [](const EnemyInfo& a, const EnemyInfo& b) noexcept {
                const float da = a.rel_x * a.rel_x + a.rel_y * a.rel_y + a.rel_z * a.rel_z;
                const float db = b.rel_x * b.rel_x + b.rel_y * b.rel_y + b.rel_z * b.rel_z;
                return da < db;
            });

            const int full_idx = pickTarget(cam_rel, dir, full_list, p);
            const bool found_any_near = std::any_of(full_list.begin(), full_list.end(), [](const EnemyInfo& e) noexcept {
                return (e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z) <= (8.0f * 8.0f);
            });

            throttle_.record(now_ms, current_pos, !found_any_near);

            if (full_idx >= 0) {
                res.list = std::move(full_list);
                res.index = full_idx;
                res.was_fallback = true;
                std::lock_guard<std::mutex> g(mutex_);
                cached_enemies_ = res.list;
            } else {
                res.list = std::move(full_list);
            }
        }

        return res;
    }

    struct NearestResult {
        EnemyInfo info{};
        bool found{false};
    };

    // Finds nearest hostile within max_distance
    NearestResult findNearestHostileWithFallback(float max_distance,
                                                const IMemoryReader& reader, uintptr_t image_base,
                                                uint64_t now_ms, const float player_feet[3]) {
        NearestResult res;
        std::vector<EnemyInfo> list;
        {
            std::lock_guard<std::mutex> g(mutex_);
            list = cached_enemies_;
        }

        auto find_best = [max_distance](const std::vector<EnemyInfo>& l) -> const EnemyInfo* {
            const EnemyInfo* best = nullptr;
            float best_d = max_distance;
            for (const auto& e : l) {
                if (!e.hostile || e.hp <= 0) continue;
                const float d = std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z);
                if (d <= best_d) {
                    best_d = d;
                    best = &e;
                }
            }
            return best;
        };

        const EnemyInfo* best = find_best(list);
        if (best == nullptr) {
            const mc::Vec3 current_pos{player_feet[0], player_feet[1], player_feet[2]};
            if (!throttle_.shouldThrottle(now_ms, current_pos, 200, 0.5f)) {
                std::vector<EnemyInfo> full_list;
                if (enumerateEnemies(reader, image_base, full_list, 2000)) {
                    std::sort(full_list.begin(), full_list.end(), [](const EnemyInfo& a, const EnemyInfo& b) noexcept {
                        const float da = a.rel_x * a.rel_x + a.rel_y * a.rel_y + a.rel_z * a.rel_z;
                        const float db = b.rel_x * b.rel_x + b.rel_y * b.rel_y + b.rel_z * b.rel_z;
                        return da < db;
                    });
                    best = find_best(full_list);
                    const bool found_any_near = std::any_of(full_list.begin(), full_list.end(), [](const EnemyInfo& e) noexcept {
                        return (e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z) <= (25.0f * 25.0f);
                    });
                    throttle_.record(now_ms, current_pos, !found_any_near);
                    if (best != nullptr) {
                        res.info = *best;
                        res.found = true;
                        std::lock_guard<std::mutex> g(mutex_);
                        cached_enemies_ = std::move(full_list);
                        return res;
                    }
                }
            }
        } else {
            res.info = *best;
            res.found = true;
            return res;
        }

        return res;
    }

private:
    mutable std::mutex mutex_;
    std::vector<EnemyInfo> cached_enemies_;
    mc::ScanThrottle throttle_;
};

} // namespace eldenring::live
