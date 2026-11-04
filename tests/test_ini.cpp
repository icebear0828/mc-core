#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "mc/ini.hpp"

TEST(Ini, ParsesSectionsKeysCommentsAndCrlf) {
    const mc::IniFile ini = mc::IniFile::Parse(
        "# header comment\r\n"
        "global=1\r\n"
        "\r\n"
        "[Core]\r\n"
        "overlay = 1\r\n"
        "; another comment\r\n"
        "[mc]\r\n"
        "hide_native=1\r\n");

    ASSERT_EQ(ini.MalformedLines(), 0u);
    ASSERT_EQ(ini.Entries().size(), 3u);
    EXPECT_EQ(ini.Entries()[0].section, "");
    EXPECT_EQ(ini.Entries()[0].key, "global");
    EXPECT_EQ(ini.Entries()[1].section, "core");
    EXPECT_EQ(ini.Entries()[1].key, "overlay");
    EXPECT_EQ(ini.Entries()[1].value, "1");
    EXPECT_EQ(ini.Entries()[2].section, "mc");
    EXPECT_EQ(ini.Entries()[2].key, "hide_native");
}

TEST(Ini, LastValueWins) {
    const mc::IniFile ini = mc::IniFile::Parse("[mc]\nreach=4\nreach=5\n");
    ASSERT_NE(ini.Get("mc", "reach"), nullptr);
    EXPECT_EQ(*ini.Get("mc", "reach"), "5");
    EXPECT_TRUE(ini.Has("MC", "REACH"));
    EXPECT_FALSE(ini.Has("ow", "reach"));
}

TEST(Ini, InlineCommentsAreStrippedOnlyAtTokenBoundaries) {
    const mc::IniFile ini = mc::IniFile::Parse("a=1 # note\nb=#ff00ff\nc=2;note\n");
    ASSERT_NE(ini.Get("", "a"), nullptr);
    EXPECT_EQ(*ini.Get("", "a"), "1");
    ASSERT_NE(ini.Get("", "b"), nullptr);
    EXPECT_EQ(*ini.Get("", "b"), "#ff00ff");
    ASSERT_NE(ini.Get("", "c"), nullptr);
    EXPECT_EQ(*ini.Get("", "c"), "2;note");
}

TEST(Ini, MalformedLinesAreCountedNotFatal) {
    const mc::IniFile ini = mc::IniFile::Parse("[core\noverlay=1\n=2\nok=3\n");
    EXPECT_EQ(ini.MalformedLines(), 2u);
    ASSERT_NE(ini.Get("", "ok"), nullptr);
    EXPECT_EQ(*ini.Get("", "ok"), "3");
}

TEST(Ini, BoolParsingAcceptsTheDocumentedWords) {
    bool value = false;
    EXPECT_TRUE(mc::ParseBool("1", value));
    EXPECT_TRUE(value);
    EXPECT_TRUE(mc::ParseBool(" TRUE ", value));
    EXPECT_TRUE(value);
    EXPECT_TRUE(mc::ParseBool("on", value));
    EXPECT_TRUE(value);
    EXPECT_TRUE(mc::ParseBool("0", value));
    EXPECT_FALSE(value);
    EXPECT_TRUE(mc::ParseBool("disabled", value));
    EXPECT_FALSE(value);
    EXPECT_FALSE(mc::ParseBool("maybe", value));
    EXPECT_FALSE(mc::ParseBool("", value));
}

TEST(Ini, IntParsingAcceptsBaseZeroAndRejectsJunk) {
    long value = 0;
    EXPECT_TRUE(mc::ParseInt("0x100A1", value));
    EXPECT_EQ(value, 0x100A1L);
    EXPECT_TRUE(mc::ParseInt(" 42 ", value));
    EXPECT_EQ(value, 42L);
    EXPECT_FALSE(mc::ParseInt("42x", value));
    EXPECT_FALSE(mc::ParseInt("", value));
}

TEST(Ini, FloatParsingRejectsJunkAndNonFinite) {
    float value = 0.f;
    EXPECT_TRUE(mc::ParseFloat(" 1.5 ", value));
    EXPECT_FLOAT_EQ(value, 1.5f);
    EXPECT_FALSE(mc::ParseFloat("1.5x", value));
    EXPECT_FALSE(mc::ParseFloat("nan", value));
    EXPECT_FALSE(mc::ParseFloat("inf", value));
    EXPECT_FALSE(mc::ParseFloat("", value));
}

TEST(Ini, SetValuesReplacesInsideTheSectionOnly) {
    const std::string text =
        "# keep me\n"
        "[core]\n"
        "overlay=1\n"
        "\n"
        "[ow]\n"
        "ow_fov=68\n"
        "ow_scale=1\n";
    const std::string out = mc::IniSetValues(text, "ow", {{"ow_fov", "70"}});

    EXPECT_NE(out.find("# keep me"), std::string::npos);
    EXPECT_NE(out.find("[core]\noverlay=1"), std::string::npos);
    EXPECT_NE(out.find("ow_fov=70"), std::string::npos);
    EXPECT_EQ(out.find("ow_fov=68"), std::string::npos);
    EXPECT_NE(out.find("ow_scale=1"), std::string::npos);
}

TEST(Ini, SetValuesAppendsMissingKeyToTheExistingSection) {
    const std::string text = "[core]\noverlay=1\n\n[ow]\now_fov=68\n\n[mc]\nblocks=1\n";
    const std::string out = mc::IniSetValues(text, "ow", {{"ow_fov", "70"}, {"ow_scale", "2"}});

    const std::size_t fov = out.find("ow_fov=70");
    const std::size_t scale = out.find("ow_scale=2");
    const std::size_t mc = out.find("[mc]");
    ASSERT_NE(fov, std::string::npos);
    ASSERT_NE(scale, std::string::npos);
    ASSERT_NE(mc, std::string::npos);
    EXPECT_LT(fov, mc);
    EXPECT_LT(scale, mc);
}

TEST(Ini, SetValuesCreatesAMissingSection) {
    const std::string out = mc::IniSetValues("[core]\noverlay=1\n", "ow", {{"ow_fov", "70"}});
    EXPECT_NE(out.find("[core]\noverlay=1"), std::string::npos);
    EXPECT_NE(out.find("[ow]\now_fov=70"), std::string::npos);
}

TEST(Ini, SetValuesHandlesGlobalKeysForLegacyFiles) {
    const std::string out = mc::IniSetValues("# tuning\now_fov=68\now_scale=1\n", "", {{"ow_fov", "70"}, {"ow_recoil_kick", "0.05"}});
    EXPECT_NE(out.find("# tuning"), std::string::npos);
    EXPECT_NE(out.find("ow_fov=70"), std::string::npos);
    EXPECT_NE(out.find("ow_scale=1"), std::string::npos);
    EXPECT_NE(out.find("ow_recoil_kick=0.05"), std::string::npos);
}

TEST(Ini, SetValuesPreservesCrlf) {
    const std::string out = mc::IniSetValues("[ow]\r\now_fov=68\r\n", "ow", {{"ow_scale", "2"}});
    EXPECT_NE(out.find("ow_fov=68\r\n"), std::string::npos);
    EXPECT_NE(out.find("ow_scale=2\r\n"), std::string::npos);
}
