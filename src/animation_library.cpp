#include "mc/animation_library.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <numbers>
#include <sstream>

namespace mc::anim {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Helper: Euler angles in degrees (X=pitch, Y=yaw, Z=roll) to Quat
Quat quatFromEulerDegrees(const Vec3& deg) {
    // In Blockbench / Minecraft Bedrock models, positive X rotation pitches arms/legs forward.
    // In canonical rig frame (X forward, Y left, Z up), pitching forward requires negative canonical pitch rotation about Y.
    const float pitch = -deg.x * (kPi / 180.0f);
    const float yaw   = deg.y * (kPi / 180.0f);
    const float roll  = deg.z * (kPi / 180.0f);

    const float cy = std::cos(yaw * 0.5f);
    const float sy = std::sin(yaw * 0.5f);
    const float cp = std::cos(pitch * 0.5f);
    const float sp = std::sin(pitch * 0.5f);
    const float cr = std::cos(roll * 0.5f);
    const float sr = std::sin(roll * 0.5f);

    Quat q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    return q;
}

// SLERP between two quaternions
Quat slerp(const Quat& a, const Quat& b_in, float t) {
    Quat b = b_in;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
        dot = -dot;
    }
    if (dot > 0.9995f) {
        Quat res{
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z),
            a.w + t * (b.w - a.w)
        };
        const float len = std::sqrt(res.x * res.x + res.y * res.y + res.z * res.z + res.w * res.w);
        if (len > 1e-6f) {
            res.x /= len; res.y /= len; res.z /= len; res.w /= len;
        }
        return res;
    }
    const float theta = std::acos(dot);
    const float sin_theta = std::sin(theta);
    const float w1 = std::sin((1.0f - t) * theta) / sin_theta;
    const float w2 = std::sin(t * theta) / sin_theta;
    return {
        a.x * w1 + b.x * w2,
        a.y * w1 + b.y * w2,
        a.z * w1 + b.z * w2,
        a.w * w1 + b.w * w2
    };
}

// =========================================================================================
// Minimal robust JSON parser for BBModel
// =========================================================================================

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type{JsonType::Null};
    bool bool_val{false};
    double num_val{0.0};
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::vector<std::pair<std::string, JsonValue>> obj_val;

    [[nodiscard]] const JsonValue* get(std::string_view key) const {
        if (type != JsonType::Object) return nullptr;
        for (const auto& [k, v] : obj_val) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    [[nodiscard]] double asDouble(double def = 0.0) const {
        if (type == JsonType::Number) return num_val;
        if (type == JsonType::String) {
            try { return std::stod(str_val); } catch (...) {}
        }
        return def;
    }

    [[nodiscard]] float asFloat(float def = 0.0f) const {
        return static_cast<float>(asDouble(def));
    }

    [[nodiscard]] bool asBool(bool def = false) const {
        return type == JsonType::Bool ? bool_val : def;
    }

    [[nodiscard]] std::string asString(std::string def = "") const {
        return type == JsonType::String ? str_val : def;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : in_(input) {}

    std::optional<JsonValue> parse() {
        skipWhitespace();
        if (pos_ >= in_.size()) return std::nullopt;
        auto val = parseValue();
        skipWhitespace();
        return val;
    }

private:
    void skipWhitespace() {
        while (pos_ < in_.size() && (std::isspace(static_cast<unsigned char>(in_[pos_])) || in_[pos_] == '\0')) {
            ++pos_;
        }
    }

    std::optional<JsonValue> parseValue() {
        skipWhitespace();
        if (pos_ >= in_.size()) return std::nullopt;
        const char c = in_[pos_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
        return std::nullopt;
    }

    std::optional<JsonValue> parseObject() {
        ++pos_; // consume '{'
        JsonValue val;
        val.type = JsonType::Object;
        skipWhitespace();
        if (pos_ < in_.size() && in_[pos_] == '}') {
            ++pos_;
            return val;
        }
        while (pos_ < in_.size()) {
            skipWhitespace();
            if (pos_ >= in_.size() || in_[pos_] != '"') return std::nullopt;
            auto key = parseString();
            if (!key) return std::nullopt;
            skipWhitespace();
            if (pos_ >= in_.size() || in_[pos_] != ':') return std::nullopt;
            ++pos_; // consume ':'
            auto member_val = parseValue();
            if (!member_val) return std::nullopt;
            val.obj_val.emplace_back(std::move(key->str_val), std::move(*member_val));
            skipWhitespace();
            if (pos_ >= in_.size()) return std::nullopt;
            if (in_[pos_] == ',') {
                ++pos_;
            } else if (in_[pos_] == '}') {
                ++pos_;
                return val;
            } else {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseArray() {
        ++pos_; // consume '['
        JsonValue val;
        val.type = JsonType::Array;
        skipWhitespace();
        if (pos_ < in_.size() && in_[pos_] == ']') {
            ++pos_;
            return val;
        }
        while (pos_ < in_.size()) {
            auto elem = parseValue();
            if (!elem) return std::nullopt;
            val.arr_val.push_back(std::move(*elem));
            skipWhitespace();
            if (pos_ >= in_.size()) return std::nullopt;
            if (in_[pos_] == ',') {
                ++pos_;
            } else if (in_[pos_] == ']') {
                ++pos_;
                return val;
            } else {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseString() {
        ++pos_; // consume '"'
        std::string res;
        while (pos_ < in_.size()) {
            const char c = in_[pos_++];
            if (c == '"') {
                JsonValue val;
                val.type = JsonType::String;
                val.str_val = std::move(res);
                return val;
            }
            if (c == '\\') {
                if (pos_ >= in_.size()) return std::nullopt;
                const char esc = in_[pos_++];
                switch (esc) {
                    case '"': res += '"'; break;
                    case '\\': res += '\\'; break;
                    case '/': res += '/'; break;
                    case 'b': res += '\b'; break;
                    case 'f': res += '\f'; break;
                    case 'n': res += '\n'; break;
                    case 'r': res += '\r'; break;
                    case 't': res += '\t'; break;
                    case 'u':
                        if (pos_ + 4 > in_.size()) return std::nullopt;
                        pos_ += 4;
                        res += '?';
                        break;
                    default: return std::nullopt;
                }
            } else {
                res += c;
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseBool() {
        if (in_.substr(pos_, 4) == "true") {
            pos_ += 4;
            JsonValue v; v.type = JsonType::Bool; v.bool_val = true; return v;
        }
        if (in_.substr(pos_, 5) == "false") {
            pos_ += 5;
            JsonValue v; v.type = JsonType::Bool; v.bool_val = false; return v;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull() {
        if (in_.substr(pos_, 4) == "null") {
            pos_ += 4;
            JsonValue v; v.type = JsonType::Null; return v;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber() {
        const size_t start = pos_;
        if (pos_ < in_.size() && in_[pos_] == '-') ++pos_;
        while (pos_ < in_.size() && (std::isdigit(static_cast<unsigned char>(in_[pos_])) || in_[pos_] == '.' ||
                                    in_[pos_] == 'e' || in_[pos_] == 'E' || in_[pos_] == '+' || in_[pos_] == '-')) {
            ++pos_;
        }
        const std::string num_str(in_.substr(start, pos_ - start));
        try {
            double d = std::stod(num_str);
            JsonValue v; v.type = JsonType::Number; v.num_val = d; return v;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string_view in_;
    size_t pos_{0};
};

// Helper: map UUIDs to group names recursively
void buildGroupUuidMap(const JsonValue& node, std::unordered_map<std::string, std::string>& out_map) {
    if (node.type != JsonType::Object) return;
    const auto* name_v = node.get("name");
    const auto* uuid_v = node.get("uuid");
    if (name_v && uuid_v && name_v->type == JsonType::String && uuid_v->type == JsonType::String) {
        out_map[uuid_v->str_val] = name_v->str_val;
    }
    const auto* children = node.get("children");
    if (children && children->type == JsonType::Array) {
        for (const auto& child : children->arr_val) {
            buildGroupUuidMap(child, out_map);
        }
    }
}

} // namespace

// =========================================================================================
// Keyframe sampling implementation
// =========================================================================================

Vec3 sampleKeyframes(const std::vector<VectorKeyframe>& frames, float time, const Vec3& default_val) {
    if (frames.empty()) return default_val;
    if (frames.size() == 1 || time <= frames.front().time) return frames.front().value;
    if (time >= frames.back().time) return frames.back().value;

    // Find interval [i, i+1]
    size_t i = 0;
    while (i + 1 < frames.size() && frames[i + 1].time < time) {
        ++i;
    }
    const auto& k0 = frames[i];
    const auto& k1 = frames[i + 1];
    const float duration = k1.time - k0.time;
    if (duration <= 1e-6f) return k1.value;

    const float t = (time - k0.time) / duration;
    // Linear interpolation
    return {
        k0.value.x + t * (k1.value.x - k0.value.x),
        k0.value.y + t * (k1.value.y - k0.value.y),
        k0.value.z + t * (k1.value.z - k0.value.z)
    };
}

// =========================================================================================
// AnimationLibrary parser from .bbmodel JSON
// =========================================================================================

std::optional<AnimationLibrary> AnimationLibrary::fromBbmodelJson(std::string_view json_str) {
    JsonParser parser(json_str);
    auto root = parser.parse();
    if (!root || root->type != JsonType::Object) return std::nullopt;

    // Build UUID -> bone_name lookup from outliner
    std::unordered_map<std::string, std::string> uuid_to_name;
    const auto* outliner = root->get("outliner");
    if (outliner && outliner->type == JsonType::Array) {
        for (const auto& node : outliner->arr_val) {
            buildGroupUuidMap(node, uuid_to_name);
        }
    }

    const auto* animations = root->get("animations");
    if (!animations || animations->type != JsonType::Array) return std::nullopt;

    AnimationLibrary lib;
    for (const auto& anim_val : animations->arr_val) {
        if (anim_val.type != JsonType::Object) continue;

        AnimationClip clip;
        const auto* name_v = anim_val.get("name");
        if (name_v) clip.name = name_v->asString();
        const auto* len_v = anim_val.get("length");
        if (len_v) clip.length = len_v->asFloat();
        const auto* loop_v = anim_val.get("loop");
        if (loop_v) {
            clip.loop = (loop_v->asString() == "loop");
            clip.hold_on_last_frame = (loop_v->asString() == "hold");
        }

        const auto* animators = anim_val.get("animators");
        if (animators && animators->type == JsonType::Object) {
            for (const auto& [uuid, track_obj] : animators->obj_val) {
                if (track_obj.type != JsonType::Object) continue;
                const auto* keyframes = track_obj.get("keyframes");
                if (!keyframes || keyframes->type != JsonType::Array) continue;

                BoneAnimationTrack track;
                const auto name_it = uuid_to_name.find(uuid);
                track.bone_name = (name_it != uuid_to_name.end()) ? name_it->second : uuid;

                for (const auto& kf_val : keyframes->arr_val) {
                    if (kf_val.type != JsonType::Object) continue;
                    const auto* ch_v = kf_val.get("channel");
                    const auto* time_v = kf_val.get("time");
                    const auto* dp_v = kf_val.get("data_points");
                    if (!ch_v || !time_v || !dp_v || dp_v->type != JsonType::Array || dp_v->arr_val.empty()) continue;

                    const std::string channel = ch_v->asString();
                    const float kf_time = time_v->asFloat();
                    const auto& dp0 = dp_v->arr_val.front();

                    Vec3 kf_val_vec{0.0f, 0.0f, 0.0f};
                    if (dp0.type == JsonType::Object) {
                        const auto* x_v = dp0.get("x");
                        const auto* y_v = dp0.get("y");
                        const auto* z_v = dp0.get("z");
                        if (x_v) kf_val_vec.x = x_v->asFloat();
                        if (y_v) kf_val_vec.y = y_v->asFloat();
                        if (z_v) kf_val_vec.z = z_v->asFloat();
                    }

                    VectorKeyframe kf{kf_time, kf_val_vec, "linear"};
                    const auto* interp_v = kf_val.get("interpolation");
                    if (interp_v) kf.interpolation = interp_v->asString("linear");

                    if (channel == "rotation") {
                        track.rotation_frames.push_back(kf);
                    } else if (channel == "position") {
                        track.position_frames.push_back(kf);
                    } else if (channel == "scale") {
                        track.scale_frames.push_back(kf);
                    }
                }

                // Sort keyframes by time
                auto sortByTime = [](VectorKeyframe& a, VectorKeyframe& b) { return a.time < b.time; };
                std::sort(track.rotation_frames.begin(), track.rotation_frames.end(), sortByTime);
                std::sort(track.position_frames.begin(), track.position_frames.end(), sortByTime);
                std::sort(track.scale_frames.begin(), track.scale_frames.end(), sortByTime);

                clip.tracks.push_back(std::move(track));
            }
        }

        lib.addClip(std::move(clip));
    }

    return lib;
}

// =========================================================================================
// ClipPlayer Implementation
// =========================================================================================

bool ClipPlayer::play(std::string_view clip_name, float blend_time, bool loop) {
    if (!library_) return false;
    const auto* next_clip = library_->findClip(clip_name);
    if (!next_clip) return false;

    if (current_clip_ == next_clip) {
        loop_ = loop;
        return true;
    }

    if (blend_time > 0.0f && current_clip_ != nullptr) {
        prev_clip_ = current_clip_;
        prev_time_ = current_time_;
        blend_duration_ = blend_time;
        blend_elapsed_ = 0.0f;
    } else {
        prev_clip_ = nullptr;
        blend_duration_ = 0.0f;
        blend_elapsed_ = 0.0f;
    }

    current_clip_ = next_clip;
    current_clip_name_ = std::string(clip_name);
    current_time_ = 0.0f;
    loop_ = loop;
    return true;
}

void ClipPlayer::update(float dt) {
    if (!current_clip_) return;

    current_time_ += dt;
    if (current_clip_->length > 0.0f) {
        if (loop_) {
            current_time_ = std::fmod(current_time_, current_clip_->length);
        } else if (current_time_ > current_clip_->length) {
            current_time_ = current_clip_->length;
        }
    }

    if (prev_clip_ && blend_duration_ > 0.0f) {
        prev_time_ += dt;
        if (prev_clip_->length > 0.0f) {
            prev_time_ = std::fmod(prev_time_, prev_clip_->length);
        }
        blend_elapsed_ += dt;
        if (blend_elapsed_ >= blend_duration_) {
            prev_clip_ = nullptr;
            blend_duration_ = 0.0f;
        }
    }
}

void ClipPlayer::setTime(float time) {
    current_time_ = time;
    if (current_clip_ && current_clip_->length > 0.0f) {
        if (loop_) {
            current_time_ = std::fmod(current_time_, current_clip_->length);
            if (current_time_ < 0.0f) current_time_ += current_clip_->length;
        } else {
            current_time_ = std::clamp(current_time_, 0.0f, current_clip_->length);
        }
    }
}

EvaluatedBonePose ClipPlayer::sampleBone(std::string_view bone_name) const {
    EvaluatedBonePose res;
    if (!current_clip_) return res;

    // Sample current clip
    const auto* track = current_clip_->findTrack(bone_name);
    Vec3 curr_pos = track ? sampleKeyframes(track->position_frames, current_time_) : Vec3{};
    Vec3 curr_rot_deg = track ? sampleKeyframes(track->rotation_frames, current_time_) : Vec3{};
    Vec3 curr_scale = track ? sampleKeyframes(track->scale_frames, current_time_, {1.0f, 1.0f, 1.0f}) : Vec3{1.0f, 1.0f, 1.0f};

    Quat curr_rot = quatFromEulerDegrees(curr_rot_deg);

    // If blending from previous clip, compute smooth blend
    if (prev_clip_ && blend_duration_ > 0.0f) {
        const float alpha = std::clamp(blend_elapsed_ / blend_duration_, 0.0f, 1.0f);
        const auto* prev_track = prev_clip_->findTrack(bone_name);
        Vec3 prev_pos = prev_track ? sampleKeyframes(prev_track->position_frames, prev_time_) : Vec3{};
        Vec3 prev_rot_deg = prev_track ? sampleKeyframes(prev_track->rotation_frames, prev_time_) : Vec3{};
        Vec3 prev_scale = prev_track ? sampleKeyframes(prev_track->scale_frames, prev_time_, {1.0f, 1.0f, 1.0f}) : Vec3{1.0f, 1.0f, 1.0f};

        Quat prev_rot = quatFromEulerDegrees(prev_rot_deg);

        res.position = {
            prev_pos.x + alpha * (curr_pos.x - prev_pos.x),
            prev_pos.y + alpha * (curr_pos.y - prev_pos.y),
            prev_pos.z + alpha * (curr_pos.z - prev_pos.z)
        };
        res.rotation = slerp(prev_rot, curr_rot, alpha);
        res.scale = {
            prev_scale.x + alpha * (curr_scale.x - prev_scale.x),
            prev_scale.y + alpha * (curr_scale.y - prev_scale.y),
            prev_scale.z + alpha * (curr_scale.z - prev_scale.z)
        };
    } else {
        res.position = curr_pos;
        res.rotation = curr_rot;
        res.scale = curr_scale;
    }

    return res;
}

SteveAnimator::PartTransforms ClipPlayer::sampleSteveParts() const {
    SteveAnimator::PartTransforms transforms{};

    // Map StevePart to bone names from .bbmodel
    auto samplePart = [&](StevePart part, std::string_view bone_name) {
        const auto pose = sampleBone(bone_name);
        Transform t;
        t.pos = pose.position;
        t.rot = pose.rotation;
        t.scale = pose.scale;
        transforms[static_cast<size_t>(part)] = t;
    };

    samplePart(StevePart::Head, "head");
    samplePart(StevePart::Hat, "head"); // hat follows head
    samplePart(StevePart::Body, "upper");
    samplePart(StevePart::Jacket, "upper");
    samplePart(StevePart::RightArm, "rightArm");
    samplePart(StevePart::RightSleeve, "rightArm");
    samplePart(StevePart::LeftArm, "leftArm");
    samplePart(StevePart::LeftSleeve, "leftArm");
    samplePart(StevePart::RightLeg, "rightLeg");
    samplePart(StevePart::RightPants, "rightLeg");
    samplePart(StevePart::LeftLeg, "leftLeg");
    samplePart(StevePart::LeftPants, "leftLeg");

    return transforms;
}

// =========================================================================================
// SteveAnimationStateMachine Implementation
// =========================================================================================

void SteveAnimationStateMachine::update(float dt, const SteveAnimInput& input) {
    last_input_ = input;
    const float speed = std::sqrt(input.forward_speed * input.forward_speed + input.strafe_speed * input.strafe_speed);

    // High level state priority determination:
    std::string_view target_clip = "idle";
    float blend_time = 0.15f;

    if (input.is_gliding) {
        target_clip = "elytra_fly";
        blend_time = 0.2f;
    } else if (input.eating_progress > 0.0f) {
        target_clip = "eat";
        blend_time = 0.1f;
    } else if (input.is_blocking) {
        target_clip = "block_shield";
        blend_time = 0.1f;
    } else if (input.bow_charge > 0.0f) {
        target_clip = "bow_pull";
        blend_time = 0.1f;
    } else if (input.swing_progress > 0.0f) {
        target_clip = "attack";
        blend_time = 0.05f;
    } else if (input.is_crouching) {
        target_clip = (speed > 0.1f) ? "sneak" : "sneak";
        blend_time = 0.12f;
    } else if (speed > 4.5f) { // sprint threshold
        target_clip = "sprint";
        blend_time = 0.15f;
    } else if (speed > 0.1f) {
        target_clip = "walk";
        blend_time = 0.15f;
    } else {
        target_clip = "idle";
        blend_time = 0.2f;
    }

    const bool is_attack = (input.swing_progress > 0.0f);
    player_.play(target_clip, blend_time, !is_attack);

    if (is_attack) {
        // Drive attack animation directly from Minecraft melee swing_progress [0, 1]
        // This ensures the punch/slash syncs perfectly with player hit cadence (attack clip is 0.4s)
        player_.setTime(input.swing_progress * 0.4f);
    } else if (target_clip == "walk" || target_clip == "sprint" || (target_clip == "sneak" && speed > 0.1f)) {
        // Velocity-based stride frequency (authentically matching Steve's walking cadence)
        // Reference walk speed is ~4.317 m/s for a 1.0s clip
        const float playback_rate = std::clamp(speed / 4.317f, 0.5f, 2.5f);
        player_.update(dt * playback_rate);
    } else {
        player_.update(dt);
    }
}

SteveAnimator::PartTransforms SteveAnimationStateMachine::getTransforms() const {
    auto transforms = player_.sampleSteveParts();

    // If look angles are present, layer them onto head and hat
    if (std::abs(last_input_.look_pitch) > 1e-4f || std::abs(last_input_.look_yaw) > 1e-4f) {
        const Vec3 head_deg{last_input_.look_pitch * (180.0f / kPi), last_input_.look_yaw * (180.0f / kPi), 0.0f};
        const Quat head_rot = quatFromEulerDegrees(head_deg);

        auto applyHead = [&](StevePart part) {
            auto& t = transforms[static_cast<size_t>(part)];
            // q_new = head_rot * q_base
            Quat q;
            q.w = head_rot.w * t.rot.w - head_rot.x * t.rot.x - head_rot.y * t.rot.y - head_rot.z * t.rot.z;
            q.x = head_rot.w * t.rot.x + head_rot.x * t.rot.w + head_rot.y * t.rot.z - head_rot.z * t.rot.y;
            q.y = head_rot.w * t.rot.y - head_rot.x * t.rot.z + head_rot.y * t.rot.w + head_rot.z * t.rot.x;
            q.z = head_rot.w * t.rot.z + head_rot.x * t.rot.y - head_rot.y * t.rot.x + head_rot.z * t.rot.w;
            t.rot = q;
        };

        applyHead(StevePart::Head);
        applyHead(StevePart::Hat);
    }

    // Scale limb swing amplitude with movement speed when walking or sneaking,
    // exactly reproducing SteveAnimator's authentic Minecraft physics feel.
    if (player_.currentClip() == "walk" || player_.currentClip() == "sneak") {
        const float speed = std::sqrt(last_input_.forward_speed * last_input_.forward_speed +
                                      last_input_.strafe_speed * last_input_.strafe_speed);
        // At normal speed (~4.3 m/s) scale is 1.0; at slow creeping speeds scale smoothly decreases.
        const float limb_scale = std::clamp(speed / 4.317f, 0.2f, 1.0f);
        if (limb_scale < 0.999f) {
            const Quat identity{0.0f, 0.0f, 0.0f, 1.0f};
            auto scaleLimb = [&](StevePart part) {
                auto& t = transforms[static_cast<size_t>(part)];
                t.rot = slerp(identity, t.rot, limb_scale);
            };
            scaleLimb(StevePart::RightArm);
            scaleLimb(StevePart::RightSleeve);
            scaleLimb(StevePart::LeftArm);
            scaleLimb(StevePart::LeftSleeve);
            scaleLimb(StevePart::RightLeg);
            scaleLimb(StevePart::RightPants);
            scaleLimb(StevePart::LeftLeg);
            scaleLimb(StevePart::LeftPants);
        }
    }

    return transforms;
}

} // namespace mc::anim

