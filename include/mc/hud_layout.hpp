#pragma once

namespace mc {

struct HudRect {
    float x{0.f}, y{0.f}, w{0.f}, h{0.f};
};

// Vanilla Minecraft GUI placement for a given screen size. Every length is an exact integer multiple of
// the GUI scale, so nearest-neighbour sprites stay crisp. Coordinates are in screen pixels, origin top-left.
class HudLayout {
public:
    HudLayout(float screen_w, float screen_h);

    // 1080p -> 3 ("Large"), 720p -> 2, 1440p -> 4. Never below 1.
    static int guiScaleFor(float screen_h);

    [[nodiscard]] int scale() const { return scale_; }

    [[nodiscard]] HudRect crosshair() const;       // 15x15 GUI px, centred
    [[nodiscard]] HudRect hotbar() const;          // 182x22, centred on the bottom edge
    [[nodiscard]] HudRect selection(int slot) const; // 24x23 frame around slot 0..8
    [[nodiscard]] HudRect item(int slot) const;    // 16x16 icon inside slot 0..8
    [[nodiscard]] HudRect heart(int index) const;  // 9x9 at an 8px pitch (icons overlap by 1 GUI px); 0 = leftmost
    [[nodiscard]] HudRect food(int index) const;   // 9x9, right-aligned to the hotbar; 0 = rightmost
    [[nodiscard]] HudRect xpBar() const;           // 182x5, directly above the hotbar
    [[nodiscard]] float xpLevelTextTop() const;    // top of the level number (Minecraft: height - 31 - 4)
    [[nodiscard]] float xpLevelCentreX() const;    // the number is centred on the screen
    [[nodiscard]] float itemNameBaselineY() const; // top of the selected-item label
    [[nodiscard]] float textSize() const { return 8.0f * static_cast<float>(scale_); } // Minecraft's font is 8 GUI px tall

private:
    [[nodiscard]] HudRect box(float gui_x, float gui_y, float gui_w, float gui_h) const;

    float screen_w_;
    float screen_h_;
    int scale_;
    float hotbar_left_; // screen px
};

} // namespace mc
