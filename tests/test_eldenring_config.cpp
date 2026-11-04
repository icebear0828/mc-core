#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "eldenring_config.hpp"

namespace {

class AdapterConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "mc_core_eldenring_config_test";
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        eldenring::AdapterConfig::Instance().ResetForTesting();
    }

    void TearDown() override {
        eldenring::AdapterConfig::Instance().ResetForTesting();
        std::filesystem::remove_all(dir_);
    }

    void Write(const std::string& name, const std::string& text) const {
        std::ofstream file(dir_ / name, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.good());
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::string GameDir() const { return dir_.string() + "/"; }

    eldenring::AdapterConfig& Load() {
        eldenring::AdapterConfig& config = eldenring::AdapterConfig::Instance();
        config.EnsureLoaded(GameDir());
        return config;
    }

    std::filesystem::path dir_;
};

} // namespace

TEST_F(AdapterConfigTest, EmptyInstallIsInert) {
    eldenring::AdapterConfig& config = Load();

    EXPECT_FALSE(config.IniPresent());
    EXPECT_FALSE(config.FeatureEnabled(eldenring::ConfigFeature::Overlay));
    EXPECT_FALSE(config.FeatureEnabled(eldenring::ConfigFeature::Input));
    EXPECT_FALSE(config.FeatureEnabled(eldenring::ConfigFeature::Damage));
    EXPECT_FALSE(config.FeatureEnabled(eldenring::ConfigFeature::BulletFire));
    EXPECT_TRUE(config.StartMode().empty());
    EXPECT_TRUE(config.McEntries().empty());
    EXPECT_TRUE(config.OwEntries().empty());
    EXPECT_TRUE(config.SummonMob().empty());
}

TEST_F(AdapterConfigTest, LegacySentinelsKeepWorking) {
    Write("mc_er_overlay.txt", "");
    Write("mc_er_input.txt", "");
    Write("mc_er_damage.txt", "");
    Write("mc_er_bulletfire.txt", "");
    Write("mc_er_mode.txt", "start_mode=cs\n");
    Write("mc_er_steve.txt", "hide_native=1\nreach=4\n");
    Write("mc_er_ow.txt", "ow_fov=70\n");
    Write("mc_er_summon.txt", "skeleton\n");

    eldenring::AdapterConfig& config = Load();

    EXPECT_FALSE(config.IniPresent());
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::Overlay));
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::Input));
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::Damage));
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::BulletFire));
    EXPECT_EQ(config.StartMode(), "cs");
    ASSERT_EQ(config.McEntries().size(), 2u);
    EXPECT_EQ(config.McEntries()[0].key, "hide_native");
    EXPECT_EQ(config.McEntries()[1].key, "reach");
    ASSERT_EQ(config.OwEntries().size(), 1u);
    EXPECT_EQ(config.OwEntries()[0].value, "70");
    EXPECT_EQ(config.SummonMob(), "skeleton");
}

TEST_F(AdapterConfigTest, IniAloneConfiguresEverything) {
    Write("mc_er.ini",
          "# one config file\n"
          "[core]\n"
          "overlay=1\n"
          "input=0\n"
          "damage=true\n"
          "bulletfire=on\n"
          "mode=ow\n"
          "\n"
          "[mc]\n"
          "hide_native=1\n"
          "first_person=1\n"
          "summon_mob=wolf\n"
          "\n"
          "[ow]\n"
          "ow_fov=70\n");

    eldenring::AdapterConfig& config = Load();

    EXPECT_TRUE(config.IniPresent());
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::Overlay));
    EXPECT_FALSE(config.FeatureEnabled(eldenring::ConfigFeature::Input));
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::Damage));
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::BulletFire));
    EXPECT_EQ(config.StartMode(), "ow");
    ASSERT_EQ(config.McEntries().size(), 2u);
    EXPECT_EQ(config.McEntries()[0].key, "hide_native");
    EXPECT_EQ(config.McEntries()[1].key, "first_person");
    ASSERT_EQ(config.OwEntries().size(), 1u);
    EXPECT_EQ(config.OwEntries()[0].key, "ow_fov");
    EXPECT_EQ(config.SummonMob(), "wolf");
}

TEST_F(AdapterConfigTest, IniWinsPerKeyAndLegacyFillsTheRest) {
    Write("mc_er_overlay.txt", "");
    Write("mc_er_input.txt", "");
    Write("mc_er_steve.txt", "reach=4\nblocks=1\n");
    Write("mc_er.ini",
          "[core]\n"
          "overlay=0\n"
          "\n"
          "[mc]\n"
          "reach=6\n");

    eldenring::AdapterConfig& config = Load();

    // The ini explicitly disables the overlay.
    EXPECT_FALSE(config.FeatureEnabled(eldenring::ConfigFeature::Overlay));
    // The ini says nothing about input, so the legacy sentinel still applies.
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::Input));
    // Tuning merges per key: reach is overridden, blocks is kept.
    ASSERT_EQ(config.McEntries().size(), 2u);
    EXPECT_EQ(config.McEntries()[0].key, "reach");
    EXPECT_EQ(config.McEntries()[0].value, "6");
    EXPECT_EQ(config.McEntries()[1].key, "blocks");
    EXPECT_EQ(config.McEntries()[1].value, "1");
}

TEST_F(AdapterConfigTest, MalformedGateFallsBackToTheLegacySentinel) {
    Write("mc_er_input.txt", "");
    Write("mc_er.ini", "[core]\noverlay=maybe\ninput=perhaps\n");

    eldenring::AdapterConfig& config = Load();

    EXPECT_FALSE(config.FeatureEnabled(eldenring::ConfigFeature::Overlay));
    EXPECT_TRUE(config.FeatureEnabled(eldenring::ConfigFeature::Input));
    ASSERT_FALSE(config.Warnings().empty());
}

TEST_F(AdapterConfigTest, InvalidModeFallsBackToTheLegacyModeFile) {
    Write("mc_er_mode.txt", "start_mode=mc\n");
    Write("mc_er.ini", "[core]\nmode=banana\n");

    eldenring::AdapterConfig& config = Load();

    EXPECT_EQ(config.StartMode(), "mc");
    ASSERT_FALSE(config.Warnings().empty());
}

TEST_F(AdapterConfigTest, LegacyModeFileDefaultsToMcWhenTheKeyIsMissing) {
    Write("mc_er_mode.txt", "# empty file\n");

    eldenring::AdapterConfig& config = Load();

    EXPECT_EQ(config.StartMode(), "mc");
}

TEST_F(AdapterConfigTest, SummonMobCanBeDisabledFromTheIni) {
    Write("mc_er_summon.txt", "skeleton\n");
    Write("mc_er.ini", "[mc]\nsummon_mob=\n");

    eldenring::AdapterConfig& config = Load();

    EXPECT_TRUE(config.SummonMob().empty());
}

TEST_F(AdapterConfigTest, PersistOwKeysWritesIntoTheIniAndKeepsOtherContent) {
    Write("mc_er.ini",
          "# keep this comment\n"
          "[core]\n"
          "overlay=1\n"
          "\n"
          "[ow]\n"
          "ow_fov=68\n"
          "ow_scale=1\n"
          "\n"
          "[mc]\n"
          "blocks=1\n");

    eldenring::AdapterConfig& config = Load();
    ASSERT_TRUE(config.PersistOwKeys({{"ow_fov", "70"}, {"ow_recoil_kick", "0.05"}}));

    std::ifstream file(dir_ / "mc_er.ini", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("# keep this comment"), std::string::npos);
    EXPECT_NE(text.find("[core]\noverlay=1"), std::string::npos);
    EXPECT_NE(text.find("[mc]\nblocks=1"), std::string::npos);
    EXPECT_NE(text.find("ow_fov=70"), std::string::npos);
    EXPECT_EQ(text.find("ow_fov=68"), std::string::npos);
    EXPECT_NE(text.find("ow_scale=1"), std::string::npos);
    EXPECT_NE(text.find("ow_recoil_kick=0.05"), std::string::npos);
}

TEST_F(AdapterConfigTest, PersistOwKeysFallsBackToTheLegacyFile) {
    Write("mc_er_ow.txt", "# tuning\now_fov=68\n");

    eldenring::AdapterConfig& config = Load();
    ASSERT_TRUE(config.PersistOwKeys({{"ow_fov", "72"}, {"ow_scale", "2"}}));

    std::ifstream file(dir_ / "mc_er_ow.txt", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("# tuning"), std::string::npos);
    EXPECT_NE(text.find("ow_fov=72"), std::string::npos);
    EXPECT_NE(text.find("ow_scale=2"), std::string::npos);
}
