#include <gtest/gtest.h>

#include "sekiro_input_filter.hpp"

#include <cstring>
#include <vector>

using namespace sekiro::input;

namespace {

// DIMOUSESTATE: lX, lY, lZ (3 x LONG) then BYTE rgbButtons[4]. DIMOUSESTATE2 has 8 buttons.
std::vector<uint8_t> mouseState(size_t size, int32_t dx, int32_t dy, std::initializer_list<uint8_t> buttons) {
    std::vector<uint8_t> s(size, 0);
    std::memcpy(s.data(), &dx, 4);
    std::memcpy(s.data() + 4, &dy, 4);
    size_t i = 12;
    for (uint8_t b : buttons) s[i++] = b;
    return s;
}

struct BufferedElement { // the leading fields of DIDEVICEOBJECTDATA: dwOfs, dwData, then the rest
    uint32_t ofs;
    uint32_t data;
    uint32_t pad[4];
};

} // namespace

TEST(SekiroInputFilterTest, ClearsOnlyTheLeftAndRightButtonsOfAMouseState) {
    auto s = mouseState(20, 7, -3, {0x80, 0x80, 0x80, 0x80, 0x80});
    suppressMouseButtons(s.data(), s.size());
    EXPECT_EQ(s[12], 0);
    EXPECT_EQ(s[13], 0);
    EXPECT_EQ(s[14], 0x80) << "middle button is not a combat input";
    EXPECT_EQ(s[15], 0x80);
    int32_t dx = 0, dy = 0;
    std::memcpy(&dx, s.data(), 4);
    std::memcpy(&dy, s.data() + 4, 4);
    EXPECT_EQ(dx, 7) << "camera movement must stay";
    EXPECT_EQ(dy, -3);
}

TEST(SekiroInputFilterTest, AcceptsBothMouseStateSizesAndIgnoresEverythingElse) {
    for (size_t size : {size_t{16}, size_t{20}}) {
        auto s = mouseState(size, 1, 1, {0x80, 0x80});
        suppressMouseButtons(s.data(), s.size());
        EXPECT_EQ(s[12], 0) << size;
        EXPECT_EQ(s[13], 0) << size;
    }
    // A keyboard state is 256 bytes: touching bytes 12/13 would eat keys. It must be left alone.
    std::vector<uint8_t> keyboard(256, 0x80);
    suppressMouseButtons(keyboard.data(), keyboard.size());
    EXPECT_EQ(keyboard[12], 0x80);
    EXPECT_EQ(keyboard[13], 0x80);
    // Too short / null never crash.
    uint8_t tiny[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    suppressMouseButtons(tiny, sizeof(tiny));
    EXPECT_EQ(tiny[7], 8);
    suppressMouseButtons(nullptr, 20);
}

TEST(SekiroInputFilterTest, BufferedMouseEventsForTheTwoButtonsAreZeroedAndOthersKept) {
    BufferedElement e[4] = {
        {kMouseButton0Offset, 0x80, {}},  // left down
        {kMouseButton1Offset, 0x80, {}},  // right down
        {8 /* lZ wheel */, 120, {}},
        {kMouseButton0Offset + 2 /* middle */, 0x80, {}},
    };
    suppressBufferedMouseButtons(e, 4, sizeof(BufferedElement));
    EXPECT_EQ(e[0].data, 0u);
    EXPECT_EQ(e[1].data, 0u);
    EXPECT_EQ(e[2].data, 120u);
    EXPECT_EQ(e[3].data, 0x80u);
    EXPECT_EQ(e[0].ofs, kMouseButton0Offset) << "the event itself stays (a zeroed button means release)";
}

TEST(SekiroInputFilterTest, BufferedFilterRespectsTheElementStrideAndRefusesNonsense) {
    // DIDEVICEOBJECTDATA is 24 bytes on x64; an unknown stride must not corrupt memory.
    std::vector<uint8_t> raw(24 * 3, 0);
    for (int i = 0; i < 3; ++i) {
        const uint32_t ofs = (i == 1) ? kMouseButton1Offset : 4;
        const uint32_t data = 0x80;
        std::memcpy(raw.data() + i * 24, &ofs, 4);
        std::memcpy(raw.data() + i * 24 + 4, &data, 4);
    }
    suppressBufferedMouseButtons(raw.data(), 3, 24);
    uint32_t d0, d1, d2;
    std::memcpy(&d0, raw.data() + 4, 4);
    std::memcpy(&d1, raw.data() + 24 + 4, 4);
    std::memcpy(&d2, raw.data() + 48 + 4, 4);
    EXPECT_EQ(d0, 0x80u);
    EXPECT_EQ(d1, 0u);
    EXPECT_EQ(d2, 0x80u);

    suppressBufferedMouseButtons(raw.data(), 3, 4);   // stride smaller than the fields we touch
    suppressBufferedMouseButtons(nullptr, 3, 24);
    suppressBufferedMouseButtons(raw.data(), 0, 24);
}


// ---------------------------------------------------------------------------------------------
// Keyboard: the healing gourd is R. Only the listed DirectInput scan codes are cleared; everything else a
// player types (movement, our own hotkeys, menus) is untouched.
// ---------------------------------------------------------------------------------------------
TEST(SekiroInputFilterTest, ClearsOnlyTheListedKeysOfAKeyboardState) {
    std::vector<uint8_t> keys(256, 0);
    keys[kDikR] = 0x80;
    keys[0x11] = 0x80; // W
    keys[0x12] = 0x80; // E
    suppressKeyboardKeys(keys.data(), keys.size());
    EXPECT_EQ(keys[kDikR], 0);
    EXPECT_EQ(keys[0x11], 0x80);
    EXPECT_EQ(keys[0x12], 0x80) << "E is interact/open door: not combat";
}

TEST(SekiroInputFilterTest, KeyboardFilterOnlyActsOnA256ByteState) {
    std::vector<uint8_t> mouse(20, 0x80);
    suppressKeyboardKeys(mouse.data(), mouse.size());
    EXPECT_EQ(mouse[kDikR], 0x80) << "a mouse state must never be treated as a keyboard";
    std::vector<uint8_t> shorter(100, 0x80);
    suppressKeyboardKeys(shorter.data(), shorter.size());
    EXPECT_EQ(shorter[kDikR], 0x80);
    suppressKeyboardKeys(nullptr, 256);
}

TEST(SekiroInputFilterTest, BufferedKeyEventsForTheListedKeysBecomeReleases) {
    BufferedElement e[3] = {
        {kDikR, 0x80, {}},  // R down
        {0x11, 0x80, {}},   // W down
        {kDikR, 0x00, {}},  // R up
    };
    suppressBufferedKeys(e, 3, sizeof(BufferedElement));
    EXPECT_EQ(e[0].data, 0u);
    EXPECT_EQ(e[1].data, 0x80u);
    EXPECT_EQ(e[2].data, 0u);
    EXPECT_EQ(e[0].ofs, kDikR);
    suppressBufferedKeys(nullptr, 3, 24);
    suppressBufferedKeys(e, 0, 24);
    suppressBufferedKeys(e, 3, 4);
}

TEST(SekiroInputFilterTest, TheSuppressedKeyListIsExactlyTheHealingGourd) {
    EXPECT_EQ(kSuppressedKeyCount, 1u);
    EXPECT_EQ(kSuppressedKeys[0], kDikR);
    EXPECT_EQ(kDikR, 0x13u); // DIK_R
}
