#pragma once

// Minecraft experience: level curve and XP bar progress. Pure logic, unit-tested.

#include <algorithm>

namespace mc {

class XpModel {
public:
    // Experience points to get from `level` to the next one.
    static int pointsForNextLevel(int level) {
        if (level >= 30) return 112 + (level - 30) * 9;
        if (level >= 15) return 37 + (level - 15) * 5;
        return 7 + level * 2;
    }

    [[nodiscard]] int level() const { return level_; }
    [[nodiscard]] float progress() const {
        const int required = pointsForNextLevel(level_);
        return required > 0 ? static_cast<float>(points_) / static_cast<float>(required) : 0.f;
    }

    // Adds points; returns how many levels were gained.
    int add(int points) {
        if (points <= 0) return 0;
        points_ += points;
        int ups = 0;
        while (points_ >= pointsForNextLevel(level_)) {
            points_ -= pointsForNextLevel(level_);
            ++level_;
            ++ups;
        }
        return ups;
    }

private:
    int level_{0};
    int points_{0};
};

// Standard Minecraft hostile mob gives 5; tough enemies or bosses give more.
inline int xpForKill(int victim_max_hp) {
    if (victim_max_hp >= 10000) return 50;
    if (victim_max_hp >= 3000) return 20;
    return 5;
}

} // namespace mc
