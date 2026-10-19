#include "cs_weapon_registry.hpp"

namespace eldenring::cs {

CSWeaponRegistry& CSWeaponRegistry::instance() {
    static CSWeaponRegistry s_instance;
    return s_instance;
}

CSWeaponRegistry::CSWeaponRegistry() {
    resetToDefaults();
}

void CSWeaponRegistry::resetToDefaults() {
    clear();
    registerWeapon(createButterflyKnifeDefinition());
    registerWeapon(createAK47Definition());
    registerWeapon(createAWPDefinition());
    registerWeapon(createM4A1SDefinition());
    active_weapon_id_ = "weapon_knife_butterfly";
}


bool CSWeaponRegistry::registerWeapon(const CSWeaponDefinition& def) {
    if (def.id.empty()) return false;
    weapons_[def.id] = def;
    if (active_weapon_id_.empty()) {
        active_weapon_id_ = def.id;
    }
    return true;
}

bool CSWeaponRegistry::unregisterWeapon(const std::string& id) {
    auto it = weapons_.find(id);
    if (it == weapons_.end()) return false;
    weapons_.erase(it);
    if (active_weapon_id_ == id) {
        active_weapon_id_ = weapons_.empty() ? "" : weapons_.begin()->first;
    }
    return true;
}

const CSWeaponDefinition* CSWeaponRegistry::findWeapon(const std::string& id) const {
    auto it = weapons_.find(id);
    if (it != weapons_.end()) {
        return &it->second;
    }
    return nullptr;
}

const CSWeaponDefinition* CSWeaponRegistry::getActiveWeapon() const {
    return findWeapon(active_weapon_id_);
}

bool CSWeaponRegistry::setActiveWeapon(const std::string& id) {
    if (weapons_.find(id) != weapons_.end()) {
        active_weapon_id_ = id;
        return true;
    }
    return false;
}

size_t CSWeaponRegistry::getWeaponCount() const {
    return weapons_.size();
}

std::vector<std::string> CSWeaponRegistry::getRegisteredWeaponIds() const {
    std::vector<std::string> ids;
    ids.reserve(weapons_.size());
    for (const auto& [id, _] : weapons_) {
        ids.push_back(id);
    }
    return ids;
}

void CSWeaponRegistry::clear() {
    weapons_.clear();
    active_weapon_id_.clear();
}

CSWeaponDefinition CSWeaponRegistry::createButterflyKnifeDefinition() {
    CSWeaponDefinition w;
    w.id = "weapon_knife_butterfly";
    w.display_name = "Butterfly Knife";
    w.type = WeaponType::KnifeButterfly;
    w.slot = WeaponSlot::Melee;
    w.model_csvm_path = "mods/mc_adapter/cs_butterfly_viewmodel.bin";
    w.albedo_tex_path = "mods/mc_adapter/knife_butterfly.png";
    w.normal_tex_path = "mods/mc_adapter/knife_butterfly_normal.png";
    w.pbr_tex_path = "mods/mc_adapter/knife_butterfly_pbr.png";

    // Sockets
    w.sockets = {
        {"wpnHand_R", "hand_R", {0.f, -0.1524f, -0.1016f}, {0.5f, 0.5f, 0.5f, 0.5f}},
        {"weapon_offset", "weapon", {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}
    };

    // Audio
    w.audio.draw_sound = "sounds/weapons/knife/knife_deploy1.wav";
    w.audio.inspect_sound = "sounds/weapons/knife/knife_inspect.wav";
    w.audio.stab_sound = "sounds/weapons/knife/knife_stab.wav";
    w.audio.hit_sound = "sounds/weapons/knife/knife_hit1.wav";

    // Ballistics
    w.ballistics.damage = 65.0f;
    w.ballistics.fire_rate_rpm = 120.0f;
    w.ballistics.max_range_meters = 2.0f;
    w.ballistics.is_automatic = false;
    w.ballistics.has_scope = false;

    w.animated_bones = {"blade", "front", "rear", "lock"};
    return w;
}

CSWeaponDefinition CSWeaponRegistry::createAK47Definition() {
    CSWeaponDefinition w;
    w.id = "weapon_ak47";
    w.display_name = "AK-47";
    w.type = WeaponType::RifleAK47;
    w.slot = WeaponSlot::Primary;
    w.model_csvm_path = "mods/mc_adapter/cs_ak47_viewmodel.bin";
    w.albedo_tex_path = "mods/mc_adapter/textures/ak47/ak47_color.png";
    w.normal_tex_path = "mods/mc_adapter/textures/ak47/ak47_normal.png";
    w.pbr_tex_path = "mods/mc_adapter/textures/ak47/ak47_pbr.png";

    // Standard CS2 dual-hand weapon mount sockets
    w.sockets = {
        {"wpnHand_R", "hand_R", {0.02f, -0.12f, -0.05f}, {0.f, 0.f, 0.f, 1.f}},
        {"wpnHand_L", "hand_L", {0.08f, 0.16f, -0.03f}, {0.f, 0.f, 0.f, 1.f}},
        {"muzzle_flash", "weapon", {0.0f, 0.0f, 0.45f}, {0.f, 0.f, 0.f, 1.f}}
    };

    w.audio.fire_sound = "sounds/weapons/ak47/ak47-1.wav";
    w.audio.reload_sound = "sounds/weapons/ak47/ak47_clipout.wav";
    w.audio.draw_sound = "sounds/weapons/ak47/ak47_draw.wav";
    w.audio.inspect_sound = "sounds/weapons/ak47/ak47_inspect.wav";

    w.ballistics.damage = 36.0f;
    w.ballistics.tracer_frequency = 0; // tracers are disabled by design; only bullet holes are shown
    w.ballistics.fire_rate_rpm = 600.0f;
    w.ballistics.max_range_meters = 300.0f;
    w.ballistics.spread_deg = 0.45f;
    w.ballistics.magazine_capacity = 30;
    w.ballistics.reserve_capacity = 90;
    w.ballistics.is_automatic = true;
    w.ballistics.has_scope = false;

    w.animated_bones = {"weapon", "magazine", "bolt"};
    return w;
}

CSWeaponDefinition CSWeaponRegistry::createAWPDefinition() {
    CSWeaponDefinition w;
    w.id = "weapon_awp";
    w.display_name = "AWP";
    w.type = WeaponType::SniperAWP;
    w.slot = WeaponSlot::Primary;
    w.model_csvm_path = "mods/mc_adapter/cs_awp_viewmodel.bin";
    w.albedo_tex_path = "mods/mc_adapter/textures/awp/awp_color.png";
    w.normal_tex_path = "mods/mc_adapter/textures/awp/awp_normal.png";
    w.pbr_tex_path = "mods/mc_adapter/textures/awp/awp_pbr.png";

    w.sockets = {
        {"wpnHand_R", "hand_R", {0.015f, -0.11f, -0.06f}, {0.f, 0.f, 0.f, 1.f}},
        {"wpnHand_L", "hand_L", {0.09f, 0.20f, -0.04f}, {0.f, 0.f, 0.f, 1.f}},
        {"muzzle_flash", "weapon", {0.0f, 0.0f, 0.65f}, {0.f, 0.f, 0.f, 1.f}}
    };

    w.audio.fire_sound = "sounds/weapons/awp/awp1.wav";
    w.audio.reload_sound = "sounds/weapons/awp/awp_clipout.wav";
    w.audio.draw_sound = "sounds/weapons/awp/awp_draw.wav";
    w.audio.inspect_sound = "sounds/weapons/awp/awp_inspect.wav";

    w.ballistics.damage = 115.0f;
    w.ballistics.tracer_frequency = 0; // tracers are disabled by design
    w.ballistics.fire_rate_rpm = 41.0f;
    w.ballistics.max_range_meters = 500.0f;
    w.ballistics.spread_deg = 0.05f;
    w.ballistics.magazine_capacity = 5;
    w.ballistics.reserve_capacity = 30;
    w.ballistics.is_automatic = false;
    w.ballistics.has_scope = true;

    w.animated_bones = {"weapon", "bolt", "magazine"};
    return w;
}

CSWeaponDefinition CSWeaponRegistry::createM4A1SDefinition() {
    CSWeaponDefinition w;
    w.id = "weapon_m4a1_silencer";
    w.display_name = "M4A1-S";
    w.type = WeaponType::RifleM4A1S;
    w.slot = WeaponSlot::Primary;
    w.model_csvm_path = "mods/mc_adapter/cs_m4a1_s_viewmodel.bin";
    w.albedo_tex_path = "mods/mc_adapter/textures/m4a1_s/m4a1_s_color.png";
    w.normal_tex_path = "mods/mc_adapter/textures/m4a1_s/m4a1_s_normal.png";
    w.pbr_tex_path = "mods/mc_adapter/textures/m4a1_s/m4a1_s_pbr.png";

    w.sockets = {
        {"wpnHand_R", "hand_R", {0.02f, -0.12f, -0.05f}, {0.f, 0.f, 0.f, 1.f}},
        {"wpnHand_L", "hand_L", {0.08f, 0.17f, -0.03f}, {0.f, 0.f, 0.f, 1.f}},
        {"silencer", "weapon", {0.0f, 0.0f, 0.40f}, {0.f, 0.f, 0.f, 1.f}}
    };

    w.audio.fire_sound = "sounds/weapons/m4a1_silencer/m4a1_silencer_01.wav";
    w.audio.reload_sound = "sounds/weapons/m4a1_silencer/m4a1_clipout.wav";
    w.audio.draw_sound = "sounds/weapons/m4a1_silencer/m4a1_draw.wav";
    w.audio.inspect_sound = "sounds/weapons/m4a1_silencer/m4a1_inspect.wav";

    w.ballistics.damage = 38.0f;
    w.ballistics.tracer_frequency = 0; // tracers are disabled by design
    w.ballistics.fire_rate_rpm = 600.0f;
    w.ballistics.max_range_meters = 350.0f;
    w.ballistics.spread_deg = 0.35f;
    w.ballistics.magazine_capacity = 20;
    w.ballistics.reserve_capacity = 80;
    w.ballistics.is_automatic = true;
    w.ballistics.has_scope = false;

    w.animated_bones = {"weapon", "silencer", "magazine", "charging_handle"};
    return w;
}

} // namespace eldenring::cs
