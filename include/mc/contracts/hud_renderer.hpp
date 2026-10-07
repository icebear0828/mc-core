#pragma once

#include "mc/hud.hpp"

namespace mc {

class IHudRenderer {
public:
    virtual ~IHudRenderer() = default;

    // Render crosshair at screen center
    virtual void renderCrosshair(float screen_w, float screen_h) = 0;

    // Render 9-slot hotbar with active items and selection cursor
    virtual void renderHotbar(float screen_w, float screen_h, const HudEngine& hud) = 0;

    // Render health heart containers (10 hearts)
    virtual void renderHearts(float start_x, float start_y, const HeartContainers& hearts) = 0;

    // Render hunger drumstick containers (10 drumsticks)
    virtual void renderHunger(float start_x, float start_y, const HungerContainers& hunger) = 0;
};

} // namespace mc
