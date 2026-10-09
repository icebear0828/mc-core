#include "mc/entity_model.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>

namespace mc::model {
namespace {

// =========================================================================================
// Minimal robust JSON parser (C++20, standard library only)
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
        return type == JsonType::Number ? num_val : def;
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
        if (!val || pos_ != in_.size()) return std::nullopt;
        return val;
    }

private:
    std::string_view in_;
    size_t pos_{0};

    void skipWhitespace() {
        while (pos_ < in_.size() && (std::isspace(static_cast<unsigned char>(in_[pos_])) != 0)) {
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
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
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
        ++pos_; // consume opening '"'
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
                    case 'u': {
                        // Skip 4 hex digits for simplicity
                        if (pos_ + 4 > in_.size()) return std::nullopt;
                        pos_ += 4;
                        res += '?';
                        break;
                    }
                    default: return std::nullopt;
                }
            } else {
                res += c;
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber() {
        const size_t start = pos_;
        if (in_[pos_] == '-') ++pos_;
        while (pos_ < in_.size() && (in_[pos_] >= '0' && in_[pos_] <= '9')) ++pos_;
        if (pos_ < in_.size() && in_[pos_] == '.') {
            ++pos_;
            while (pos_ < in_.size() && (in_[pos_] >= '0' && in_[pos_] <= '9')) ++pos_;
        }
        if (pos_ < in_.size() && (in_[pos_] == 'e' || in_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < in_.size() && (in_[pos_] == '+' || in_[pos_] == '-')) ++pos_;
            while (pos_ < in_.size() && (in_[pos_] >= '0' && in_[pos_] <= '9')) ++pos_;
        }
        const std::string_view num_str = in_.substr(start, pos_ - start);
        double val = 0.0;
        std::stringstream ss;
        ss << num_str;
        if (!(ss >> val)) return std::nullopt;
        JsonValue jv;
        jv.type = JsonType::Number;
        jv.num_val = val;
        return jv;
    }

    std::optional<JsonValue> parseBool() {
        if (in_.substr(pos_).starts_with("true")) {
            pos_ += 4;
            JsonValue val;
            val.type = JsonType::Bool;
            val.bool_val = true;
            return val;
        }
        if (in_.substr(pos_).starts_with("false")) {
            pos_ += 5;
            JsonValue val;
            val.type = JsonType::Bool;
            val.bool_val = false;
            return val;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull() {
        if (in_.substr(pos_).starts_with("null")) {
            pos_ += 4;
            JsonValue val;
            val.type = JsonType::Null;
            return val;
        }
        return std::nullopt;
    }
};

// =========================================================================================
// Mesh builder helper for Bedrock cubes
// =========================================================================================

Vec3 bedrockToCanonicalCm(float x, float y, float z) {
    constexpr float s = rig::kCmPerModelPixel;
    // Bedrock coords: X left, Y up from feet, Z back.
    // Canonical rig frame: X forward (-Z), Y left (+X), Z up (+Y).
    return {-z * s, x * s, y * s};
}

float dot3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

struct FaceDef {
    Vec3 corners[4]; // in model pixels
    float u, v, w, h;
};

std::array<FaceDef, 6> cubeFaces(const Cube& c) {
    const float x0 = c.origin.x - c.inflate, y0 = c.origin.y - c.inflate, z0 = c.origin.z - c.inflate;
    const float x1 = c.origin.x + c.size.x + c.inflate, y1 = c.origin.y + c.size.y + c.inflate,
                z1 = c.origin.z + c.size.z + c.inflate;
    const float u = c.u, v = c.v, w = c.size.x, h = c.size.y, d = c.size.z;

    const float u_west = c.mirror ? (u + d + w) : u;
    const float u_east = c.mirror ? u : (u + d + w);

    return {{
        // North / Front (-Z)
        {{{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}, u + d, v + d, w, h},
        // South / Back (+Z)
        {{{x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}}, u + d + w + d, v + d, w, h},
        // West (-X)
        {{{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}}, u_west, v + d, d, h},
        // East (+X)
        {{{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}}, u_east, v + d, d, h},
        // Top (+Y)
        {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, u + d, v, w, d},
        // Bottom (-Y)
        {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}, u + d + w, v, w, d},
    }};
}

void appendCubeMesh(const Cube& cube, const rig::HostBasis& basis, float tex_w, float tex_h, rig::RigMesh& mesh) {
    const auto faces = cubeFaces(cube);
    const float inv_w = 1.0f / (tex_w > 0.0f ? tex_w : 64.0f);
    const float inv_h = 1.0f / (tex_h > 0.0f ? tex_h : 64.0f);

    Vec3 centre{};
    std::vector<Vec3> positions;
    positions.reserve(24);
    for (const auto& f : faces) {
        for (const auto& corner : f.corners) {
            const Vec3 p = basis.fromCanonical(bedrockToCanonicalCm(corner.x, corner.y, corner.z));
            positions.push_back(p);
            centre = centre + p;
        }
    }
    centre = centre * (1.0f / static_cast<float>(positions.size()));

    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& f = faces[fi];
        const auto base = static_cast<uint16_t>(mesh.vertices.size());
        const float s_of[4] = {0, 1, 1, 0};
        const float t_of[4] = {1, 1, 0, 0}; // UV top-left convention

        for (size_t c = 0; c < 4; ++c) {
            const Vec3 p = positions[fi * 4 + c];
            mesh.vertices.push_back({
                p.x, p.y, p.z,
                (f.u + s_of[c] * f.w) * inv_w,
                (f.v + t_of[c] * f.h) * inv_h,
            });
        }

        const Vec3 a = positions[fi * 4 + 0], b = positions[fi * 4 + 1], c2 = positions[fi * 4 + 2];
        const Vec3 n = cross(b - a, c2 - a);
        const bool outward = dot3(n, a - centre) > 0.0f;
        const uint16_t tri[6] = {0, 1, 2, 0, 2, 3};
        const uint16_t flipped[6] = {0, 2, 1, 0, 3, 2};
        for (uint16_t idx : outward ? tri : flipped) {
            mesh.indices.push_back(static_cast<uint16_t>(base + idx));
        }
    }
}

} // namespace

// =========================================================================================
// EntityModel implementation
// =========================================================================================

const Bone* EntityModel::findBone(std::string_view name) const {
    for (const auto& b : bones) {
        if (b.name == name) return &b;
    }
    return nullptr;
}

rig::RigMesh EntityModel::buildBoneMesh(const Bone& bone, const rig::HostBasis& basis) const {
    rig::RigMesh mesh;
    mesh.vertices.reserve(bone.cubes.size() * 24);
    mesh.indices.reserve(bone.cubes.size() * 36);
    for (const auto& cube : bone.cubes) {
        appendCubeMesh(cube, basis, texture_width, texture_height, mesh);
    }
    return mesh;
}

rig::RigMesh EntityModel::buildBoneMesh(std::string_view bone_name, const rig::HostBasis& basis) const {
    const auto* bone = findBone(bone_name);
    if (!bone) return {};
    return buildBoneMesh(*bone, basis);
}

rig::RigMesh EntityModel::buildCombinedMesh(const rig::HostBasis& basis) const {
    rig::RigMesh mesh;
    size_t total_cubes = 0;
    for (const auto& b : bones) total_cubes += b.cubes.size();
    mesh.vertices.reserve(total_cubes * 24);
    mesh.indices.reserve(total_cubes * 36);
    for (const auto& b : bones) {
        for (const auto& cube : b.cubes) {
            appendCubeMesh(cube, basis, texture_width, texture_height, mesh);
        }
    }
    return mesh;
}

std::optional<EntityModel> EntityModel::fromJson(std::string_view json_str, std::string_view target_id) {
    JsonParser parser(json_str);
    auto root = parser.parse();
    if (!root || root->type != JsonType::Object) return std::nullopt;

    const JsonValue* geom = nullptr;
    std::string identifier = std::string(target_id);
    float tex_w = 64.0f;
    float tex_h = 64.0f;
    const JsonValue* raw_bones = nullptr;

    // Bedrock 1.12.0+ format: "minecraft:geometry" array
    if (const auto* mg = root->get("minecraft:geometry"); mg && mg->type == JsonType::Array) {
        if (!target_id.empty()) {
            for (const auto& g : mg->arr_val) {
                if (const auto* desc = g.get("description")) {
                    if (const auto* id_val = desc->get("identifier")) {
                        if (id_val->asString() == target_id) {
                            geom = &g;
                            break;
                        }
                    }
                }
            }
        }
        if (!geom && !mg->arr_val.empty()) {
            geom = &mg->arr_val[0];
        }
        if (!geom) return std::nullopt;

        if (const auto* desc = geom->get("description")) {
            if (const auto* id_val = desc->get("identifier")) identifier = id_val->asString();
            if (const auto* tw = desc->get("texture_width")) tex_w = tw->asFloat(64.0f);
            if (const auto* th = desc->get("texture_height")) tex_h = th->asFloat(64.0f);
        }
        raw_bones = geom->get("bones");
    } else {
        // Bedrock 1.8.0 format: "geometry.<id>"
        for (const auto& [k, v] : root->obj_val) {
            if (k.starts_with("geometry.")) {
                if (target_id.empty() || k == target_id) {
                    geom = &v;
                    identifier = k;
                    break;
                }
            }
        }
        if (!geom) return std::nullopt;
        if (const auto* tw = geom->get("texturewidth")) tex_w = tw->asFloat(64.0f);
        else if (const auto* tw2 = geom->get("texture_width")) tex_w = tw2->asFloat(64.0f);
        if (const auto* th = geom->get("textureheight")) tex_h = th->asFloat(64.0f);
        else if (const auto* th2 = geom->get("texture_height")) tex_h = th2->asFloat(64.0f);
        raw_bones = geom->get("bones");
    }

    if (!raw_bones || raw_bones->type != JsonType::Array) return std::nullopt;

    EntityModel model;
    model.identifier = identifier;
    model.texture_width = tex_w;
    model.texture_height = tex_h;

    for (const auto& b : raw_bones->arr_val) {
        if (b.type != JsonType::Object) continue;
        Bone bone;
        if (const auto* n = b.get("name")) bone.name = n->asString();
        if (const auto* p = b.get("parent")) bone.parent = p->asString();
        if (const auto* piv = b.get("pivot"); piv && piv->type == JsonType::Array && piv->arr_val.size() >= 3) {
            bone.pivot = {piv->arr_val[0].asFloat(), piv->arr_val[1].asFloat(), piv->arr_val[2].asFloat()};
        }
        if (const auto* rot = b.get("rotation"); rot && rot->type == JsonType::Array && rot->arr_val.size() >= 3) {
            bone.rotation = {rot->arr_val[0].asFloat(), rot->arr_val[1].asFloat(), rot->arr_val[2].asFloat()};
        }

        const bool bone_mirror = b.get("mirror") ? b.get("mirror")->asBool() : false;

        if (const auto* cubes = b.get("cubes"); cubes && cubes->type == JsonType::Array) {
            for (const auto& c : cubes->arr_val) {
                if (c.type != JsonType::Object) continue;
                Cube cube;
                if (const auto* o = c.get("origin"); o && o->type == JsonType::Array && o->arr_val.size() >= 3) {
                    cube.origin = {o->arr_val[0].asFloat(), o->arr_val[1].asFloat(), o->arr_val[2].asFloat()};
                }
                if (const auto* s = c.get("size"); s && s->type == JsonType::Array && s->arr_val.size() >= 3) {
                    cube.size = {s->arr_val[0].asFloat(), s->arr_val[1].asFloat(), s->arr_val[2].asFloat()};
                }
                if (const auto* u = c.get("uv"); u && u->type == JsonType::Array && u->arr_val.size() >= 2) {
                    cube.u = u->arr_val[0].asFloat();
                    cube.v = u->arr_val[1].asFloat();
                }
                if (const auto* inf = c.get("inflate")) cube.inflate = inf->asFloat();
                cube.mirror = c.get("mirror") ? c.get("mirror")->asBool() : bone_mirror;
                bone.cubes.push_back(cube);
            }
        }
        model.bones.push_back(std::move(bone));
    }

    return model;
}

} // namespace mc::model
