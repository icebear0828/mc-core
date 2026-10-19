#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eldenring::cs {

enum class WeaponType {
    KnifeButterfly,
    KnifeBayonet,
    KnifeBowie,
    KnifeKarambit,
    RifleAK47,
    RifleM4A1S,
    SniperAWP,
    PistolDeagle,
    Custom
};

enum class WeaponSlot {
    Primary,   // AK-47, M4A1-S, AWP
    Secondary, // Desert Eagle, Glock
    Melee,     // Butterfly Knife, Bayonet
    Utility    // Flashbang, Smoke, HE
};

struct WeaponMountSocket {
    std::string socket_name;       // e.g. "wpnHand_R", "wpnHand_L", "weapon_offset"
    std::string target_bone;       // e.g. "hand_R", "root_motion"
    float offset[3]{0.f, 0.f, 0.f}; // Position offset in socket space (meters)
    float rotation[4]{0.f, 0.f, 0.f, 1.f}; // Quaternion (x, y, z, w)
};

struct WeaponAudioProfile {
    std::string fire_sound;
    std::string reload_sound;
    std::string draw_sound;
    std::string inspect_sound;
    std::string stab_sound;
    std::string hit_sound;
};

struct WeaponBallistics {
    float damage{100.0f};
    float fire_rate_rpm{600.0f};
    float max_range_meters{250.0f};
    float spread_deg{0.0f};
    uint32_t magazine_capacity{30};
    uint32_t reserve_capacity{90};
    bool is_automatic{true};
    bool has_scope{false};
    // Show a tracer every N rounds (CS2 m_nTracerFrequency: AK/M4 = 3, AWP = 1, 0 = never).
    uint32_t tracer_frequency{1};
};

struct CSWeaponDefinition {
    std::string id;            // Unique weapon id, e.g. "weapon_knife_butterfly", "weapon_ak47", "weapon_awp"
    std::string display_name;  // Display name, e.g. "Butterfly Knife", "AK-47", "AWP", "M4A1-S"
    WeaponType type{WeaponType::KnifeButterfly};
    WeaponSlot slot{WeaponSlot::Melee};

    std::string model_csvm_path;
    std::string albedo_tex_path;
    std::string normal_tex_path;
    std::string pbr_tex_path;

    std::vector<WeaponMountSocket> sockets;
    WeaponAudioProfile audio;
    WeaponBallistics ballistics;

    // Submesh bone classification mapping for animated parts
    std::vector<std::string> animated_bones;
};

class CSWeaponRegistry {
public:
    static CSWeaponRegistry& instance();

    bool registerWeapon(const CSWeaponDefinition& def);
    bool unregisterWeapon(const std::string& id);
    [[nodiscard]] const CSWeaponDefinition* findWeapon(const std::string& id) const;
    [[nodiscard]] const CSWeaponDefinition* getActiveWeapon() const;
    bool setActiveWeapon(const std::string& id);
    [[nodiscard]] size_t getWeaponCount() const;
    [[nodiscard]] std::vector<std::string> getRegisteredWeaponIds() const;
    void clear();
    void resetToDefaults();

    // Standard weapon definition factories
    static CSWeaponDefinition createButterflyKnifeDefinition();
    static CSWeaponDefinition createAK47Definition();
    static CSWeaponDefinition createAWPDefinition();
    static CSWeaponDefinition createM4A1SDefinition();

private:
    CSWeaponRegistry();
    std::unordered_map<std::string, CSWeaponDefinition> weapons_;
    std::string active_weapon_id_;
};

} // namespace eldenring::cs
