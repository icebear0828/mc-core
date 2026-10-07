#include "mc/hud_layout.hpp"

#include <algorithm>
#include <cmath>

namespace mc {

namespace {
constexpr float kHotbarWidth = 182.f;
constexpr float kHotbarHeight = 22.f;
constexpr float kSlotPitch = 20.f;
constexpr float kIconSize = 9.f;
constexpr float kIconPitch = 8.f;
constexpr float kStatusRowFromBottom = 39.f;
constexpr float kItemNameFromBottom = 59.f;
constexpr float kCrosshairSize = 15.f;
} // namespace

int HudLayout::guiScaleFor(float screen_h) {
    return std::max(1, static_cast<int>(std::lround(screen_h / 360.0f)));
}

HudLayout::HudLayout(float screen_w, float screen_h)
    : screen_w_(screen_w), screen_h_(screen_h), scale_(guiScaleFor(screen_h)) {
    hotbar_left_ = std::floor((screen_w_ - kHotbarWidth * static_cast<float>(scale_)) * 0.5f);
}

HudRect HudLayout::box(float gui_x, float gui_y, float gui_w, float gui_h) const {
    const float s = static_cast<float>(scale_);
    // gui_x is relative to the hotbar's left edge, gui_y relative to the screen bottom (negative = up)
    return {hotbar_left_ + gui_x * s, screen_h_ + gui_y * s, gui_w * s, gui_h * s};
}

HudRect HudLayout::crosshair() const {
    const float s = static_cast<float>(scale_);
    return {std::floor((screen_w_ - kCrosshairSize * s) * 0.5f), std::floor((screen_h_ - kCrosshairSize * s) * 0.5f),
            kCrosshairSize * s, kCrosshairSize * s};
}

HudRect HudLayout::hotbar() const { return box(0.f, -kHotbarHeight, kHotbarWidth, kHotbarHeight); }

HudRect HudLayout::selection(int slot) const {
    return box(kSlotPitch * static_cast<float>(slot) - 1.f, -kHotbarHeight - 1.f, 24.f, 23.f);
}

HudRect HudLayout::item(int slot) const {
    return box(3.f + kSlotPitch * static_cast<float>(slot), -kHotbarHeight + 3.f, 16.f, 16.f);
}

HudRect HudLayout::heart(int index) const {
    return box(kIconPitch * static_cast<float>(index), -kStatusRowFromBottom, kIconSize, kIconSize);
}

HudRect HudLayout::food(int index) const {
    return box(kHotbarWidth - kIconSize - kIconPitch * static_cast<float>(index), -kStatusRowFromBottom, kIconSize, kIconSize);
}

float HudLayout::itemNameBaselineY() const {
    return screen_h_ - kItemNameFromBottom * static_cast<float>(scale_);
}

} // namespace mc
