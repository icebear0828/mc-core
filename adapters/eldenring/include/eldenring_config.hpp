// One place that resolves the Elden Ring adapter's startup configuration.
//
// `mc_er.ini` in the game root is optional. When it names a key, that value
// wins; otherwise the legacy `mc_er_*.txt` sentinel files are honored exactly
// as before; when neither exists the feature stays off. A missing ini must
// therefore keep the old "absent config = inert" behavior: no input takeover,
// no native-model hiding, no forced mode.
//
// The class is header-only and has no Windows dependency so the resolution
// rules can be unit-tested on any platform.
#pragma once

#include <cstddef>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mc/ini.hpp"

namespace eldenring {

enum class ConfigFeature { Overlay, Input, Damage, BulletFire };

struct ConfigEntry {
    std::string key;
    std::string value;
};

class AdapterConfig {
public:
    static AdapterConfig& Instance() {
        static AdapterConfig instance;
        return instance;
    }

    // Reads mc_er.ini and the legacy sentinels from `game_dir`. Idempotent for
    // the same directory; a different directory reloads.
    void EnsureLoaded(const std::string& game_dir) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loaded_ && game_dir_ == game_dir) return;
        LoadLocked(game_dir);
    }

    void ResetForTesting() {
        std::lock_guard<std::mutex> lock(mutex_);
        loaded_ = false;
        game_dir_.clear();
        ini_present_ = false;
        ini_ = mc::IniFile{};
        start_mode_.clear();
        summon_mob_.clear();
        mc_entries_.clear();
        ow_entries_.clear();
        warnings_.clear();
        for (bool& gate : legacy_gates_) gate = false;
    }

    [[nodiscard]] bool Loaded() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loaded_;
    }

    [[nodiscard]] bool IniPresent() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ini_present_;
    }

    // ini [core] key wins; otherwise the legacy sentinel file's presence.
    [[nodiscard]] bool FeatureEnabled(ConfigFeature feature) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return FeatureEnabledLocked(feature);
    }

    // "mc", "cs" or "ow"; empty means "no startup mode was configured".
    [[nodiscard]] std::string StartMode() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return start_mode_;
    }

    // Ordered tuning keys: legacy file first, then the ini section overrides
    // matching keys in place.
    [[nodiscard]] std::vector<ConfigEntry> McEntries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return mc_entries_;
    }

    [[nodiscard]] std::vector<ConfigEntry> OwEntries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ow_entries_;
    }

    // F2 summon mob from [mc] summon_mob= or the legacy mc_er_summon.txt.
    // Empty means "no summon configured".
    [[nodiscard]] std::string SummonMob() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return summon_mob_;
    }

    [[nodiscard]] std::vector<std::string> Warnings() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return warnings_;
    }

    // Writes live OW tuning back to the active source: mc_er.ini's [ow] section
    // when the ini exists, otherwise mc_er_ow.txt. Comments and unrelated keys
    // survive.
    bool PersistOwKeys(const std::vector<ConfigEntry>& updates) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!loaded_) return false;
        std::vector<std::pair<std::string, std::string>> pairs;
        pairs.reserve(updates.size());
        for (const ConfigEntry& entry : updates) pairs.emplace_back(entry.key, entry.value);

        std::string text;
        if (ini_present_) {
            if (!ReadText(game_dir_ + "mc_er.ini", text)) return false;
            return WriteText(game_dir_ + "mc_er.ini", mc::IniSetValues(text, "ow", pairs));
        }
        ReadText(game_dir_ + "mc_er_ow.txt", text); // a missing file becomes empty
        return WriteText(game_dir_ + "mc_er_ow.txt", mc::IniSetValues(text, "", pairs));
    }

private:
    static constexpr std::size_t kFeatureCount = 4;
    static constexpr const char* kFeatureKeys[kFeatureCount] = {"overlay", "input", "damage", "bulletfire"};
    static constexpr const char* kFeatureFiles[kFeatureCount] = {"mc_er_overlay.txt", "mc_er_input.txt", "mc_er_damage.txt",
                                                                 "mc_er_bulletfire.txt"};

    AdapterConfig() = default;

    bool FeatureEnabledLocked(ConfigFeature feature) const {
        const std::size_t index = static_cast<std::size_t>(feature);
        if (ini_present_) {
            if (const std::string* raw = ini_.Get("core", kFeatureKeys[index])) {
                bool value = false;
                if (mc::ParseBool(*raw, value)) return value;
                // Malformed value: fall back to the legacy sentinel instead of
                // guessing. The warning is recorded at load time.
            }
        }
        return legacy_gates_[index];
    }

    static bool IsModeName(const std::string& value) { return value == "mc" || value == "cs" || value == "ow"; }

    static std::string LowerTrim(std::string_view text) { return mc::ini_detail::ToLower(mc::ini_detail::Trim(text)); }

    static void MergeEntry(std::vector<ConfigEntry>& entries, const std::string& key, const std::string& value) {
        for (ConfigEntry& entry : entries) {
            if (entry.key == key) {
                entry.value = value;
                return;
            }
        }
        entries.push_back(ConfigEntry{key, value});
    }

    static bool PathExists(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        return file.good();
    }

    static bool ReadText(const std::string& path, std::string& out) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

    static bool WriteText(const std::string& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }

    // The legacy files are key=value lines with no section header.
    static void AppendLegacyEntries(const std::string& path, std::vector<ConfigEntry>& entries) {
        std::string text;
        if (!ReadText(path, text)) return;
        const mc::IniFile legacy = mc::IniFile::Parse(text);
        for (const mc::IniEntry& entry : legacy.Entries()) {
            if (entry.section.empty()) MergeEntry(entries, entry.key, entry.value);
        }
    }

    void AppendSectionEntries(std::string_view section, std::vector<ConfigEntry>& entries) const {
        if (!ini_present_) return;
        for (const mc::IniEntry& entry : ini_.Entries()) {
            if (entry.section != section) continue;
            // summon_mob is a summon selector, not a Steve tuning key.
            if (section == "mc" && entry.key == "summon_mob") continue;
            MergeEntry(entries, entry.key, entry.value);
        }
    }

    static std::string MobFromLegacyText(std::string_view text) {
        const std::string content = mc::ini_detail::ToLower(text);
        if (content.find("skeleton") != std::string::npos) return "skeleton";
        if (content.find("iron_golem") != std::string::npos || content.find("golem") != std::string::npos) return "iron_golem";
        if (content.find("wolf") != std::string::npos) return "wolf";
        if (content.find("zombie") != std::string::npos) return "zombie";
        if (content.find("creeper") != std::string::npos) return "creeper";
        // The old reader defaulted to a creeper for any other (or empty) file.
        return "creeper";
    }

    void LoadLocked(const std::string& game_dir) {
        loaded_ = true;
        game_dir_ = game_dir;
        ini_present_ = false;
        ini_ = mc::IniFile{};
        start_mode_.clear();
        summon_mob_.clear();
        mc_entries_.clear();
        ow_entries_.clear();
        warnings_.clear();
        for (bool& gate : legacy_gates_) gate = false;

        std::string ini_text;
        if (ReadText(game_dir_ + "mc_er.ini", ini_text)) {
            ini_present_ = true;
            ini_ = mc::IniFile::Parse(ini_text);
            if (ini_.MalformedLines() != 0) {
                warnings_.push_back("mc_er.ini: skipped " + std::to_string(ini_.MalformedLines()) + " malformed line(s)");
            }
        }

        for (std::size_t i = 0; i < kFeatureCount; ++i) {
            legacy_gates_[i] = PathExists(game_dir_ + kFeatureFiles[i]);
        }
        if (ini_present_) {
            for (std::size_t i = 0; i < kFeatureCount; ++i) {
                if (const std::string* raw = ini_.Get("core", kFeatureKeys[i])) {
                    bool value = false;
                    if (!mc::ParseBool(*raw, value)) {
                        warnings_.push_back("mc_er.ini: [core] " + std::string(kFeatureKeys[i]) + "=" + *raw +
                                            " is not a boolean; using the legacy sentinel");
                    }
                }
            }
        }

        if (ini_present_) {
            if (const std::string* raw = ini_.Get("core", "mode")) {
                const std::string value = LowerTrim(*raw);
                if (IsModeName(value)) {
                    start_mode_ = value;
                } else {
                    warnings_.push_back("mc_er.ini: [core] mode=" + *raw + " is not mc/cs/ow; ignored");
                }
            }
        }
        if (start_mode_.empty()) {
            std::string mode_text;
            if (ReadText(game_dir_ + "mc_er_mode.txt", mode_text)) {
                const mc::IniFile legacy_mode = mc::IniFile::Parse(mode_text);
                const std::string* raw = legacy_mode.Get("", "start_mode");
                if (raw == nullptr) {
                    // The old reader defaulted an empty/partial file to MC.
                    start_mode_ = "mc";
                } else {
                    const std::string value = LowerTrim(*raw);
                    if (IsModeName(value)) {
                        start_mode_ = value;
                    } else {
                        warnings_.push_back("mc_er_mode.txt: start_mode=" + *raw + " is not mc/cs/ow; ignored");
                    }
                }
            }
        }

        AppendLegacyEntries(game_dir_ + "mc_er_steve.txt", mc_entries_);
        AppendSectionEntries("mc", mc_entries_);
        AppendLegacyEntries(game_dir_ + "mc_er_ow.txt", ow_entries_);
        AppendSectionEntries("ow", ow_entries_);

        bool summon_configured = false;
        if (ini_present_) {
            if (const std::string* raw = ini_.Get("mc", "summon_mob")) {
                summon_configured = true;
                const std::string value = LowerTrim(*raw);
                if (!value.empty() && value != "none" && value != "off" && value != "0") summon_mob_ = value;
            }
        }
        if (!summon_configured) {
            std::string summon_text;
            if (ReadText(game_dir_ + "mc_er_summon.txt", summon_text)) summon_mob_ = MobFromLegacyText(summon_text);
        }
    }

    mutable std::mutex mutex_;
    bool loaded_{false};
    std::string game_dir_;
    bool ini_present_{false};
    mc::IniFile ini_;
    std::string start_mode_;
    std::string summon_mob_;
    std::vector<ConfigEntry> mc_entries_;
    std::vector<ConfigEntry> ow_entries_;
    std::vector<std::string> warnings_;
    bool legacy_gates_[kFeatureCount]{};
};

} // namespace eldenring
