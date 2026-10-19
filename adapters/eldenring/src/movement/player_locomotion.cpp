#include "eldenring_locomotion.hpp"

#include <cstdarg>
#include <cmath>
#include <cstring>

namespace eldenring::movement {

void PlayerLocomotion::log(const char* fmt, ...) {
    if (!callbacks_.log) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    callbacks_.log(std::string(buf));
}

PlayerLocomotion::PlayerLocomotion() {
    is_key_down_ = [](int vk) -> bool {
#ifdef _WIN32
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
        (void)vk;
        return false;
#endif
    };
}

void PlayerLocomotion::resetForTesting() {
    flying_.store(false, std::memory_order_relaxed);
    fly_have_written_ = false;
    std::memset(fly_own_, 0, sizeof(fly_own_));
    std::memset(fly_last_written_, 0, sizeof(fly_last_written_));
    fly_rebases_ = 0;
    flight_tap_.reset();
    last_flight_log_ms_ = 0;

    mc_jump_.store(true, std::memory_order_relaxed);
    mc_jump_vk_.store(VK_SPACE, std::memory_order_relaxed);
    mc_jump_speed_.store(eldenring::jump::kMcJumpInitialVelocity, std::memory_order_relaxed);
    prev_jump_key_ = false;
    jump_start_ms_ = 0;
    jump_controller_.cancel();

    sprint_boost_.store(2.5f, std::memory_order_relaxed);
    fall_reset_.store(true, std::memory_order_relaxed);
    fall_hold_.store(1.0f, std::memory_order_relaxed);
    pre_fall_t_.store(0.f, std::memory_order_relaxed);
}

bool PlayerLocomotion::isKeyDown(int vk) const {
    if (is_key_down_) {
        return is_key_down_(vk);
    }
#ifdef _WIN32
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

uintptr_t PlayerLocomotion::fallModuleOf(uintptr_t chr) const {
    if (chr == 0 || !callbacks_.safe_read) return 0;
    uint64_t container = 0, fall = 0;
    if (!callbacks_.safe_read(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !callbacks_.safe_read(static_cast<uintptr_t>(container) + 0x70, &fall, sizeof(fall))) {
        return 0;
    }
    return static_cast<uintptr_t>(fall);
}

uintptr_t PlayerLocomotion::actionFlagModuleOf(uintptr_t chr) const {
    if (chr == 0 || !callbacks_.safe_read) return 0;
    uint64_t container = 0, act = 0;
    if (!callbacks_.safe_read(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !callbacks_.safe_read(static_cast<uintptr_t>(container) + 0x08, &act, sizeof(act))) {
        return 0;
    }
    return static_cast<uintptr_t>(act);
}

void PlayerLocomotion::unlockAirMovement(uintptr_t chr) const {
    if (const uintptr_t act = actionFlagModuleOf(chr)) {
        uint64_t flags = 0;
        if (callbacks_.safe_read && callbacks_.safe_read(act + 0x40, &flags, sizeof(flags)) &&
            eldenring::action::hasAirLocks(flags)) {
            const uint64_t cleared = eldenring::action::clearAirLocks(flags);
            if (callbacks_.safe_write) {
                callbacks_.safe_write(act + 0x40, &cleared, sizeof(cleared));
            }
        }
    }
}

float PlayerLocomotion::fallTimer(uintptr_t chr) const {
    float t = -1.f;
    if (const uintptr_t fall = fallModuleOf(chr)) {
        if (callbacks_.safe_read) {
            callbacks_.safe_read(fall + 0x18, &t, sizeof(t));
        }
    }
    return t;
}

void PlayerLocomotion::writeFallTimer(uintptr_t chr, float value) {
    if (const uintptr_t fall = fallModuleOf(chr)) {
        pre_fall_t_.store(fallTimer(chr), std::memory_order_relaxed);
        if (callbacks_.safe_write) {
            callbacks_.safe_write(fall + 0x18, &value, sizeof(value));
        }
    }
}

void PlayerLocomotion::resetFallTimer(uintptr_t chr) {
    if (!fall_reset_.load(std::memory_order_relaxed)) return;
    const float cur_t = fallTimer(chr);
    if (cur_t > fall_hold_.load(std::memory_order_relaxed) || eldenring::action::shouldResetFallTimer(cur_t, 0.2f)) {
        writeFallTimer(chr, 0.f);
    }
}

void PlayerLocomotion::landFallTimer(uintptr_t chr) {
    if (fall_reset_.load(std::memory_order_relaxed)) {
        writeFallTimer(chr, 0.f);
    }
    unlockAirMovement(chr);
}

void PlayerLocomotion::driveVelocity(uintptr_t module, bool force_zero, RuntimeContext& ctx) {
    float vx = 0.f, vz = 0.f;
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    if (!force_zero && fg) {
        const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
        const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
        const bool sneak_key_down = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                                    isKeyDown('X');
        const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                              ctx.combat().shield.isBlocking();
        const bool sneaking = sneak_key_down;
        const bool sprinting = !sneaking && !blocking && ctx.player().sprint_active.load(std::memory_order_relaxed);
        ctx.mode().sneak_active.store(sneaking, std::memory_order_relaxed);

        const float target_speed = eldenring::shield::effectiveMovementSpeed(
            ctx.mode().block_walk_speed.load(std::memory_order_relaxed),
            ctx.mode().sprint_speed.load(std::memory_order_relaxed),
            ctx.mode().sneak_speed.load(std::memory_order_relaxed),
            sneaking, sprinting, blocking);

        CameraPose cam{};
        if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
            float dir[2] = {0.f, 0.f};
            const bool moving = eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir);
            if (moving) {
                vx = dir[0] * target_speed;
                vz = dir[1] * target_speed;
                if (sneaking) {
                    float cur_feet[3] = {};
                    if (callbacks_.safe_read && callbacks_.safe_read(module + layout::kPhysicsPosition, cur_feet, sizeof(cur_feet))) {
                        const float test_dx = vx * 0.05f;
                        const float test_dz = vz * 0.05f;
                        if (callbacks_.clamp_sneak) {
                            const auto test_res = callbacks_.clamp_sneak(cur_feet, test_dx, test_dz);
                            if (test_res.clamped_x) vx = 0.f;
                            if (test_res.clamped_z) vz = 0.f;
                        }
                    }
                }

                float target_yaw = std::atan2(dir[0], dir[1]);
                if (ctx.mode().modOwns(mc::ControlDomain::Camera)) {
                    float fx = cam.forward[0], fz = cam.forward[2];
                    if (fx * fx + fz * fz > 1e-4f) target_yaw = std::atan2(fx, fz);
                }

                const bool instant = ctx.mode().instant_turn.load(std::memory_order_relaxed);
                float final_yaw = target_yaw;
                if (!instant) {
                    float curr_q[4] = {};
                    if (callbacks_.safe_read && callbacks_.safe_read(module + layout::kPhysicsOrientation, curr_q, sizeof(curr_q))) {
                        const float curr_yaw = eldenring::motion::headingFromCharacterQuat(
                            eldenring::motion::OrientationQuat{curr_q[0], curr_q[1], curr_q[2], curr_q[3]});
                        final_yaw = eldenring::motion::stepYaw(curr_yaw, target_yaw, 0.0166667f, ctx.mode().turn_speed.load(), false);
                    }
                }
                const auto out_q = eldenring::motion::characterQuatFromHeading(final_yaw);
                const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
                if (callbacks_.safe_write) {
                    callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
                }
            } else if (ctx.mode().modOwns(mc::ControlDomain::Camera)) {
                float fx = cam.forward[0], fz = cam.forward[2];
                if (fx * fx + fz * fz > 1e-4f) {
                    const float target_yaw = std::atan2(fx, fz);
                    float curr_q[4] = {};
                    bool should_update = true;
                    if (callbacks_.safe_read && callbacks_.safe_read(module + layout::kPhysicsOrientation, curr_q, sizeof(curr_q))) {
                        const float curr_yaw = eldenring::motion::headingFromCharacterQuat(
                            eldenring::motion::OrientationQuat{curr_q[0], curr_q[1], curr_q[2], curr_q[3]});
                        const float diff = std::abs(eldenring::motion::normalizeAngle(target_yaw - curr_yaw));
                        if (diff < 0.02f) should_update = false;
                    }
                    if (should_update && callbacks_.safe_write) {
                        const auto out_q = eldenring::motion::characterQuatFromHeading(target_yaw);
                        const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
                        callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
                    }
                }
            }
        }
    }
    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + 0x120, &vx, sizeof(vx));
        callbacks_.safe_write(module + 0x120 + 8, &vz, sizeof(vz));
    }
}

void PlayerLocomotion::stepGroundSprint(float dt, RuntimeContext& ctx) {
    const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                          ctx.combat().shield.isBlocking();
    if (!ctx.player().sprint_active.load(std::memory_order_relaxed) ||
        !ctx.mode().mc_mode.load(std::memory_order_relaxed) ||
        blocking) {
        return;
    }
    if (isFlying() || (callbacks_.is_player_airborne && callbacks_.is_player_airborne()) ||
        (callbacks_.is_player_dead && callbacks_.is_player_dead())) {
        return;
    }
    if ((callbacks_.is_game_in_foreground && !callbacks_.is_game_in_foreground()) ||
        (callbacks_.want_suppress && !callbacks_.want_suppress())) {
        return;
    }

    const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
    const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
    if (std::abs(fwd_key) < 0.1f && std::abs(right_key) < 0.1f) return;

    CameraPose cam{};
    if (!callbacks_.read_camera || !callbacks_.read_camera(cam)) return;

    float dir[2] = {0.f, 0.f};
    if (!eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir)) return;

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0) return;
    const uintptr_t module = callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0) return;

    float feet[3] = {};
    if (!callbacks_.safe_read || !callbacks_.safe_read(module + layout::kPhysicsPosition, feet, sizeof(feet))) return;

    const float boost = sprint_boost_.load(std::memory_order_relaxed);
    const float clamped_dt = std::clamp(dt, 0.001f, 0.05f);
    const float dx = dir[0] * boost * clamped_dt;
    const float dz = dir[1] * boost * clamped_dt;

    const float target_x = feet[0] + dx;
    const float target_z = feet[2] + dz;

    if (callbacks_.check_sprint_collision && callbacks_.check_sprint_collision(target_x, feet[1], target_z)) {
        return;
    }

    feet[0] = target_x;
    feet[2] = target_z;
    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, feet, sizeof(feet));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, feet, sizeof(feet));
        const uint8_t one = 1;
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, sizeof(one));
    }
}

void PlayerLocomotion::stepJump(float dt, uint64_t now_ms, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    if (!mc_jump_.load(std::memory_order_relaxed) || !ctx.mode().mc_mode.load(std::memory_order_relaxed) ||
        ctx.ui().inv_open.load(std::memory_order_relaxed) || !fg) {
        prev_jump_key_ = false;
        jump_controller_.cancel();
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0) return;
    const uintptr_t module = callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0) return;

    float feet[3] = {};
    if (!callbacks_.safe_read || !callbacks_.safe_read(module + layout::kPhysicsPosition, feet, sizeof(feet))) return;

    const bool down = isKeyDown(mc_jump_vk_.load(std::memory_order_relaxed));
    const bool edge = down && !prev_jump_key_;
    prev_jump_key_ = down;

    uint8_t ground = 0;
    if (callbacks_.safe_read) {
        callbacks_.safe_read(module + 0x92, &ground, 1);
    }
    const uint8_t zero = 0, one = 1;

    if (!jump_controller_.isActive()) {
        bool supported_by_block = false;
        if (callbacks_.is_supported_by_block) {
            supported_by_block = callbacks_.is_supported_by_block(feet);
        }
        if (!supported_by_block) {
            supported_by_block = ctx.mode().block_supported.load(std::memory_order_relaxed);
        }

        const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
        const bool can_jump = eldenring::jump::canTakeOff(
            ground == 1,
            supported_by_block,
            ctx.mode().on_ground.load(std::memory_order_relaxed),
            dead);

        if (edge && can_jump) {
            const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
            const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
            const bool sneak_key_down = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                                        isKeyDown('X');
            const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                                  ctx.combat().shield.isBlocking();
            const bool sneaking = sneak_key_down;
            const bool sprinting = !sneaking && !blocking && ctx.player().sprint_active.load(std::memory_order_relaxed);
            const float target_speed = eldenring::shield::effectiveMovementSpeed(
                ctx.mode().block_walk_speed.load(std::memory_order_relaxed),
                ctx.mode().sprint_speed.load(std::memory_order_relaxed),
                ctx.mode().sneak_speed.load(std::memory_order_relaxed),
                sneaking, sprinting, blocking);

            float init_vx = 0.f, init_vz = 0.f;
            CameraPose cam{};
            if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
                float dir[2] = {0.f, 0.f};
                if (eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir)) {
                    init_vx = dir[0] * target_speed;
                    init_vz = dir[1] * target_speed;
                }
            }

            jump_controller_.start(feet[1], init_vx, init_vz, sprinting, mc_jump_speed_.load(std::memory_order_relaxed));
            jump_start_ms_ = now_ms;
            log("mcjump: start at y=%.3f, v=%.2f m/s, sprint=%d, h_speed=%.2f m/s",
                feet[1], jump_controller_.vy(), sprinting ? 1 : 0,
                std::sqrt(jump_controller_.vx() * jump_controller_.vx() + jump_controller_.vz() * jump_controller_.vz()));
        } else {
            return;
        }
    }

    unlockAirMovement(player);
    resetFallTimer(player);

    const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
    const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
    const bool steer_sneak = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                             isKeyDown('X');
    const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                          ctx.combat().shield.isBlocking();
    const bool sprinting = !steer_sneak && !blocking && ctx.player().sprint_active.load(std::memory_order_relaxed);
    const float steer_speed = eldenring::shield::effectiveMovementSpeed(
        ctx.mode().block_walk_speed.load(std::memory_order_relaxed),
        ctx.mode().sprint_speed.load(std::memory_order_relaxed),
        ctx.mode().sneak_speed.load(std::memory_order_relaxed),
        steer_sneak, sprinting, blocking);

    float dir[2] = {0.f, 0.f};
    bool has_steer = false;
    CameraPose cam{};
    if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
        has_steer = eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir);
    }

    const auto res = jump_controller_.update(dt, dir[0], dir[1], steer_speed, has_steer);
    float ny = res.y;
    float pos[3] = {feet[0], ny, feet[2]};
    bool landed = false;
    bool landed_on_block = false;

    if (callbacks_.resolve_block) {
        const blocks::Resolve r = callbacks_.resolve_block(pos);
        if (r.moved && r.feet[1] < ny - 1e-4f) {
            pos[1] = r.feet[1];
            ny = r.feet[1];
            jump_controller_.hitCeiling(r.feet[1]);
        } else if (res.vy <= 0.f && callbacks_.is_supported_by_block && callbacks_.is_supported_by_block(pos)) {
            landed = true;
            landed_on_block = true;
        } else if (res.vy <= 0.f && callbacks_.resolve_block_swept) {
            const float prev_pos[3] = {feet[0], jump_controller_.currentY() - res.vy * dt, feet[2]};
            const blocks::Resolve sw = callbacks_.resolve_block_swept(prev_pos, pos);
            if (sw.moved && sw.standing) {
                pos[1] = sw.feet[1];
                ny = sw.feet[1];
                landed = true;
                landed_on_block = true;
            }
        }
    }

    if (!landed && res.vy <= 0.f && callbacks_.cast_ray) {
        const float from[3] = {feet[0], ny + 0.5f, feet[2]};
        const float drop = std::max(2.0f, (jump_controller_.currentY() - jump_controller_.startY()) + 2.5f);
        const float disp[3] = {0.f, -drop, 0.f};
        const RayResult r = callbacks_.cast_ray(from, disp);
        if (r.ok && r.hit) {
            const float ground_y = from[1] - r.fraction * drop;
            if (ny <= ground_y + 0.02f) {
                pos[1] = ground_y;
                ny = ground_y;
                landed = true;
                landed_on_block = false;
            }
        } else if (ny < jump_controller_.startY() - 3.0f) {
            log("mcjump: no ground under the arc, native game takes over");
            jump_controller_.cancel();
            if (callbacks_.safe_write) callbacks_.safe_write(module + 0x92, &zero, 1);
            return;
        }
    }

    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, pos, sizeof(pos));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, pos, sizeof(pos));
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, sizeof(one));

        float vx = res.vx;
        float vz = res.vz;
        callbacks_.safe_write(module + 0x120, &vx, sizeof(vx));
        callbacks_.safe_write(module + 0x120 + 8, &vz, sizeof(vz));

        if (has_steer) {
            float target_yaw = std::atan2(dir[0], dir[1]);
            if (ctx.mode().modOwns(mc::ControlDomain::Camera)) {
                float fx = cam.forward[0], fz = cam.forward[2];
                if (fx * fx + fz * fz > 1e-4f) target_yaw = std::atan2(fx, fz);
            }
            const auto out_q = eldenring::motion::characterQuatFromHeading(target_yaw);
            const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
            callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
        }

        if (landed) {
            jump_controller_.land(ny);
            const auto flags = eldenring::jump::computeLandingFlags(landed_on_block);
            const uint8_t ground_byte = flags.on_ground_flag ? 1 : 0;
            const uint8_t air_byte = flags.airborne_flag ? 1 : 0;
            const uint8_t supp_byte = flags.support_flag ? 1 : 0;
            callbacks_.safe_write(module + 0x92, &ground_byte, 1);
            callbacks_.safe_write(module + 0x1D1, &supp_byte, 1);
            callbacks_.safe_write(module + 0x1D0, &air_byte, 1);
            const float zero_f = 0.f;
            callbacks_.safe_write(module + 0x1B8, &zero_f, sizeof(zero_f));
            log("mcjump: landed %s, peak %.2f m, %.2f s in the air, fall timer was %.2f",
                landed_on_block ? "on block" : "on terrain", jump_controller_.peak(),
                static_cast<float>(now_ms - jump_start_ms_) / 1000.f, fallTimer(player));
            landFallTimer(player);
            unlockAirMovement(player);
        } else {
            callbacks_.safe_write(module + 0x92, &zero, 1);
            callbacks_.safe_write(module + 0x1D0, &one, 1);
        }
    }
}

void PlayerLocomotion::stepFlight(float dt, uint64_t now_ms, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
    const bool allowed = ctx.mode().mc_mode.load(std::memory_order_relaxed) &&
                         ctx.currentGameMode() == creative::GameMode::Creative &&
                         !ctx.ui().inv_open.load(std::memory_order_relaxed) &&
                         fg && !dead;
    if (!allowed) {
        flight_tap_.reset();
        fly_have_written_ = false;
        if (flying_.exchange(false)) {
            log("flight: stopped (creative mode, MC mode, focus, inventory or death changed)");
        }
        return;
    }

    if (flight_tap_.update(isKeyDown(VK_SPACE), now_ms)) {
        const bool on = !flying_.load(std::memory_order_relaxed);
        flying_.store(on, std::memory_order_relaxed);
        fly_have_written_ = false;
        log("flight: %s", on ? "started" : "stopped");
    }

    if (!flying_.load(std::memory_order_relaxed)) return;

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const uintptr_t module = player != 0 && callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    float read_pos[3] = {};
    if (module == 0 || !callbacks_.safe_read || !callbacks_.safe_read(module + layout::kPhysicsPosition, read_pos, sizeof(read_pos))) {
        fly_have_written_ = false;
        return;
    }

    if (!fly_have_written_) {
        std::memcpy(fly_own_, read_pos, sizeof(fly_own_));
    } else if (flight::positionOverwritten(fly_last_written_, read_pos, 3.f)) {
        const float before[3] = {fly_own_[0], fly_own_[1], fly_own_[2]};
        flight::followRebase(fly_own_, fly_last_written_, read_pos);
        if (++fly_rebases_ <= 20) {
            log("flight: coordinates re-based by the game: wrote=(%.2f %.2f %.2f) read=(%.2f %.2f %.2f) own (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)",
                fly_last_written_[0], fly_last_written_[1], fly_last_written_[2],
                read_pos[0], read_pos[1], read_pos[2],
                before[0], before[1], before[2],
                fly_own_[0], fly_own_[1], fly_own_[2]);
        }
    }

    flight::Keys keys{};
    keys.forward = isKeyDown('W');
    keys.back = isKeyDown('S');
    keys.left = isKeyDown('A');
    keys.right = isKeyDown('D');
    keys.up = isKeyDown(VK_SPACE);
    keys.down = isKeyDown(VK_SHIFT);

    flight::Heading heading{};
    CameraPose cam{};
    const bool have_heading = callbacks_.read_camera && callbacks_.read_camera(cam) &&
                              flight::headingFromCamera(cam.forward, cam.right, heading);
    if (!have_heading) {
        keys.forward = keys.back = keys.left = keys.right = false;
    }

    flight::step(fly_own_, keys, heading, dt, fly_h_speed_.load(std::memory_order_relaxed), fly_v_speed_.load(std::memory_order_relaxed));

    if (have_heading) {
        const float fly_yaw = flight::bodyYaw(heading);
        fly_yaw_.store(fly_yaw, std::memory_order_relaxed);
        const auto out_q = eldenring::motion::characterQuatFromHeading(fly_yaw);
        const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
        if (callbacks_.safe_write) {
            callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
        }
    }

    writeFallTimer(player, 0.f);
    unlockAirMovement(player);

    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, fly_own_, sizeof(fly_own_));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, fly_own_, sizeof(fly_own_));

        uint8_t sync_before = 0;
        if (callbacks_.safe_read) callbacks_.safe_read(module + layout::kPhysicsProxySyncRequest, &sync_before, 1);
        if (fly_sync_.load(std::memory_order_relaxed)) {
            const uint8_t one = 1;
            callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, 1);
        }

        if (now_ms - last_flight_log_ms_ >= 1000) {
            last_flight_log_ms_ = now_ms;
            log("flight: pos=(%.2f %.2f %.2f) read_back=(%.2f %.2f %.2f) fall_t=%.2f sync91_before=%u keys[%d%d%d%d up=%d down=%d] dt=%.4f",
                fly_own_[0], fly_own_[1], fly_own_[2], read_pos[0], read_pos[1], read_pos[2],
                fallTimer(player), static_cast<unsigned>(sync_before),
                keys.forward ? 1 : 0, keys.back ? 1 : 0, keys.left ? 1 : 0, keys.right ? 1 : 0,
                keys.up ? 1 : 0, keys.down ? 1 : 0, dt);
        }
    }

    std::memcpy(fly_last_written_, fly_own_, sizeof(fly_last_written_));
    fly_have_written_ = true;
}

} // namespace eldenring::movement
