#include "mc/damage_scaling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mc {
namespace {

// Damage inputs below zero or not finite carry no information: treat them as zero.
float sanitizeNonNegative(float value) {
    if (!std::isfinite(value) || value <= 0.f) return 0.f;
    return value;
}

// Converts an intermediate double back into a finite float; NaN and negative values become zero.
float toFiniteFloat(double value) {
    if (!(value > 0.0)) return 0.f;
    constexpr double kMaxFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (value >= kMaxFloat) return std::numeric_limits<float>::max();
    return static_cast<float>(value);
}

} // namespace

DamageScalingConfig DamageScalingConfig::validated() const {
    DamageScalingConfig out;
    out.mc_to_host_base =
        (std::isfinite(mc_to_host_base) && mc_to_host_base >= 0.f) ? mc_to_host_base : kDefaultMcToHostBase;
    out.mc_to_host_per_level = (std::isfinite(mc_to_host_per_level) && mc_to_host_per_level >= 0.f)
                                   ? mc_to_host_per_level
                                   : kDefaultMcToHostPerLevel;
    if (!std::isfinite(host_to_mc_divisor) || host_to_mc_divisor < 0.f) {
        out.host_to_mc_divisor = kDefaultHostToMcDivisor;
    } else {
        out.host_to_mc_divisor = std::max(host_to_mc_divisor, kMinHostToMcDivisor);
    }
    return out;
}

float mcToHostMultiplier(int host_level, const DamageScalingConfig& config) {
    const DamageScalingConfig cfg = config.validated();
    const int level = host_level > 0 ? host_level : 0;
    const double multiplier = static_cast<double>(cfg.mc_to_host_base) +
                              static_cast<double>(cfg.mc_to_host_per_level) * static_cast<double>(level);
    return toFiniteFloat(multiplier);
}

float scaleMcToHost(float mc_damage, int host_level, const DamageScalingConfig& config) {
    const float damage = sanitizeNonNegative(mc_damage);
    if (damage <= 0.f) return 0.f;
    const double product = static_cast<double>(damage) * static_cast<double>(mcToHostMultiplier(host_level, config));
    return toFiniteFloat(product);
}

float scaleHostToMc(float host_damage, const DamageScalingConfig& config) {
    const float damage = sanitizeNonNegative(host_damage);
    if (damage <= 0.f) return 0.f;
    const DamageScalingConfig cfg = config.validated();
    const double quotient = static_cast<double>(damage) / static_cast<double>(cfg.host_to_mc_divisor);
    return toFiniteFloat(quotient);
}

void applyScaledDamage(HitIntent& intent, int host_level, const DamageScalingConfig& config) {
    const float multiplier = mcToHostMultiplier(host_level, config);
    intent.damage = scaleMcToHost(intent.damage, host_level, config);

    // The percentage is a fraction of the victim's maximum health; scaling it with the same
    // multiplier keeps it describing the same hit as the absolute damage.
    const double percent = static_cast<double>(sanitizeNonNegative(intent.max_hp_percent)) * static_cast<double>(multiplier);
    intent.max_hp_percent = percent >= 1.0 ? 1.0f : static_cast<float>(percent);
}

HealthMirror::HealthMirror(Authority authority, float max_hp)
    : authority_(authority),
      max_hp_(sanitizeNonNegative(max_hp)),
      local_hp_(max_hp_),
      remote_fraction_(1.0f),
      last_pushed_fraction_(1.0f) {}

void HealthMirror::setAuthority(Authority authority) {
    if (authority == authority_) return;
    authority_ = authority;
    if (authority_ == Authority::Host) {
        // The host takes over: it starts from the value the MC bar currently shows.
        remote_fraction_ = localFraction();
    }
    last_pushed_fraction_ = -1.f; // out of range, so the next update always pushes
    updateDeathLatch(authority_ == Authority::Host ? remote_fraction_ : localFraction());
}

void HealthMirror::setMaxHp(float max_hp) {
    max_hp_ = sanitizeNonNegative(max_hp);
    local_hp_ = std::min(local_hp_, max_hp_);
}

void HealthMirror::setLocalHp(float hp) {
    local_hp_ = std::min(sanitizeNonNegative(hp), max_hp_);
    if (authority_ == Authority::Minecraft) {
        updateDeathLatch(localFraction());
    }
}

void HealthMirror::observeRemoteHp(float fraction) {
    if (!std::isfinite(fraction)) return; // a broken read must not move the mirror
    remote_fraction_ = std::clamp(fraction, 0.f, 1.f);
    if (authority_ == Authority::Host) {
        local_hp_ = remote_fraction_ * max_hp_;
        updateDeathLatch(remote_fraction_);
    }
}

float HealthMirror::localFraction() const {
    if (!(max_hp_ > 0.f)) return 0.f;
    return std::clamp(local_hp_ / max_hp_, 0.f, 1.f);
}

bool HealthMirror::consumeDeath() {
    const bool died = just_died_;
    just_died_ = false;
    return died;
}

bool HealthMirror::shouldPushRemoteUpdate(float new_fraction) const {
    if (authority_ != Authority::Minecraft) return false;
    if (!std::isfinite(new_fraction)) return false;
    const float clamped = std::clamp(new_fraction, 0.f, 1.f);
    return std::fabs(clamped - last_pushed_fraction_) >= kPushEpsilon;
}

void HealthMirror::markPushed() {
    last_pushed_fraction_ = localFraction();
    remote_fraction_ = last_pushed_fraction_;
}

void HealthMirror::revive() {
    dead_ = false;
    just_died_ = false;
    last_pushed_fraction_ = -1.f; // the host bar must be resynced
}

void HealthMirror::updateDeathLatch(float authoritative_fraction) {
    if (authoritative_fraction <= kDeathFraction) {
        if (!dead_) {
            dead_ = true;
            just_died_ = true;
        }
    } else if (authoritative_fraction >= kReviveFraction) {
        dead_ = false;
    }
}

} // namespace mc
