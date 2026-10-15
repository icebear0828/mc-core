#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "eldenring_runtime_context.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_steve.hpp"
#include "eldenring_shield.hpp"
#include "eldenring_survival.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_bullet.hpp"
#include "eldenring_bow.hpp"
#include "eldenring_xp.hpp"
#include "eldenring_hunger.hpp"
#include "eldenring_melee.hpp"
#include "mc/inventory.hpp"

namespace eldenring::combat {

namespace layout = ::eldenring::live::layout;
using eldenring::live::CameraPose;
using eldenring::live::Vitals;
using eldenring::live::DamageQueue;
using eldenring::live::HitTemplate;
using eldenring::live::DamageResult;
using eldenring::live::DamageOutcome;
using eldenring::live::SurvivalState;
using eldenring::live::HungerModel;
using eldenring::live::XpModel;
using eldenring::live::DrainContext;
using eldenring::live::scaleToEr;
using eldenring::live::IMemoryReader;
using eldenring::live::xpForKill;
using mc::HitFeedback;
using mc::MeleeController;
using mc::ArmorDamageReductionResult;
using mc::computeArmorDamageReduction;
using mc::TotemOutcome;
using mc::totemClamp;

struct ShieldVerdict {
    bool ok_player{false}, ok_attacker{false}, ok_orient{false}, ok_cam{false};
    bool blocks{false};
    float pp[3]{}, ap[3]{}, q[4]{};
    float body_yaw{0.f}, cam_yaw{0.f}, yaw{0.f};
    float dot{0.f}, body_dot{0.f};
};

struct CombatCallbacks {
    std::function<uintptr_t()> get_player_chr;
    std::function<bool(uintptr_t chr, float feet[3])> read_physics_pos;
    std::function<bool(uintptr_t chr, float quat[4])> read_physics_orient;
    std::function<bool(CameraPose& out_cam)> read_camera;
    std::function<bool(uintptr_t addr, void* dst, size_t size)> safe_read;
    std::function<bool(uintptr_t addr, const void* src, size_t size)> safe_write;
    std::function<bool(const float from[3], const float to[3])> has_line_of_sight;
    std::function<bool(uintptr_t chr, Vitals& out_vitals)> read_vitals;
    std::function<void(uintptr_t chr, int new_hp)> set_chr_hp;
    std::function<bool(uintptr_t dmg_module, uintptr_t attacker, void* ctx_ptr)> call_vfunc7;
    std::function<void(const char* sound, float vol)> play_sound;
    std::function<void(int fx_kind, const float pos[3], int count)> spawn_fx;
    std::function<void(const std::string&)> log;
    const IMemoryReader* reader{nullptr};
    uintptr_t image_base{0};
};

class CombatService {
public:
    static CombatService& Instance() {
        static CombatService s_instance;
        return s_instance;
    }

    void setCallbacks(CombatCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
    }

    // Shield evaluation
    ShieldVerdict evaluateShield(uintptr_t player, uintptr_t attacker_chr);

    // Natural hits on player: shield blocking, absorption, armor reduction, totem of undying
    bool playerHitSurvival(void* module, void* attacker, uint8_t* ctx_ptr, uint8_t blocked,
                           uint32_t& popped_heal_target, MeleeController& melee,
                           SurvivalState& survival,
                           HitFeedback& feedback,
                           HungerModel& hunger,
                           RuntimeContext& ctx);

    // GuardCheck, DamageVfunc7, and BreakTest hooks
    bool handleGuardCheck(void* attacker_module, void* victim_chr, RuntimeContext& ctx);
    void handleDamageVfunc7(void* damage_module, void* attacker, uint8_t* ctx_ptr, RuntimeContext& ctx);
    bool handleBreakTest(void* damage_module, uint8_t* ctx_ptr, bool original_broke, RuntimeContext& ctx);

    // Damage queue drainage
    void drainDamageQueue(DamageQueue& queue, const HitTemplate* tmpl,
                          uintptr_t updating_data_module, uint64_t tick,
                          mc::Inventory& inv, HitFeedback& feedback,
                          XpModel& xp, RuntimeContext& ctx);

    // Bullet template management
    void recordBulletTemplate(const void* request);
    void loadBulletTemplate(const std::string& game_dir);
    void saveBulletTemplate(const uint8_t* body, const std::string& game_dir);
    bool hasValidBulletTemplate() const;
    void getBulletTemplate(uint8_t out[eldenring::bullet::kFullRequestBytes]) const;

    void resetForTesting();

private:
    CombatService();

    void log(const char* fmt, ...);

    CombatCallbacks callbacks_;
    std::atomic<uint64_t> guard_sound_ms_{0};
    std::atomic<uint64_t> knockdown_log_ms_{0};

    mutable std::mutex bullet_mutex_;
    bool bullet_template_valid_{false};
    uint8_t bullet_template_[eldenring::bullet::kFullRequestBytes]{};
};

} // namespace eldenring::combat
