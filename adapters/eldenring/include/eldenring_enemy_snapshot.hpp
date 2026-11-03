#pragma once

// One enemy enumeration per frame, shared by the camera step (enemy/block collision) and the
// render-camera step (summon hiding). Both run on the game thread; the mutex only makes the
// snapshot safe if a future caller reads it from another thread.

#include "eldenring_world.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace eldenring::live {

class EnemySnapshot {
public:
    void publish(std::shared_ptr<const std::vector<EnemyInfo>> list, uint64_t now_ms) {
        std::lock_guard<std::mutex> g(mutex_);
        list_ = std::move(list);
        published_ms_ = now_ms;
    }

    // Null when nothing was published yet, or the snapshot is older than `max_age_ms`.
    [[nodiscard]] std::shared_ptr<const std::vector<EnemyInfo>> fresh(uint64_t now_ms, uint64_t max_age_ms) const {
        std::lock_guard<std::mutex> g(mutex_);
        if (list_ == nullptr) return nullptr;
        if (now_ms < published_ms_ || now_ms - published_ms_ > max_age_ms) return nullptr;
        return list_;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const std::vector<EnemyInfo>> list_;
    uint64_t published_ms_{0};
};

inline EnemySnapshot& gameThreadEnemySnapshot() {
    static EnemySnapshot snapshot;
    return snapshot;
}

} // namespace eldenring::live
