#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace mc {

struct Vec3 {
    float x{0.f};
    float y{0.f};
    float z{0.f};

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }

    [[nodiscard]] float lengthSq() const { return x * x + y * y + z * z; }
    [[nodiscard]] float length() const { return std::sqrt(lengthSq()); }

    [[nodiscard]] Vec3 normalized() const {
        const float l = length();
        if (l < 1e-6f) {
            return {0.f, 0.f, 0.f};
        }
        return {x / l, y / l, z / l};
    }
};

struct Quat {
    float x{0.f};
    float y{0.f};
    float z{0.f};
    float w{1.f};
};

struct Transform {
    Vec3 pos{};
    Quat rot{};
    Vec3 scale{1.f, 1.f, 1.f};
};

struct GridPos {
    int32_t x{0};
    int32_t y{0};
    int32_t z{0};

    constexpr bool operator==(const GridPos&) const = default;
};

enum class BlockId : uint8_t {
    Air = 0,
    Dirt,
    Stone,
    Tnt,
    Wood,
    Obsidian
};

enum class ItemId : uint16_t {
    None = 0,
    DiamondSword,
    DiamondPickaxe,
    Bow,
    Arrow,
    Trident,
    FlintAndSteel,
    EnderPearl,
    GoldenApple,
    EnchantedGoldenApple,
    Bread,
    CookedBeef,
    TotemOfUndying,
    Elytra,
    FireworkRocket,
    BlockDirt,
    BlockStone,
    BlockTnt
};

enum class EffectType : uint8_t {
    Regeneration,
    Absorption,
    Resistance,
    FireResistance
};

struct ActiveEffect {
    EffectType type{EffectType::Regeneration};
    int level{1};
    float duration{0.f}; // seconds
};

struct RaycastResult {
    bool has_hit{false};
    Vec3 point{};
    Vec3 normal{};
    uint64_t hit_entity_id{0};
    bool is_block{false};
};

} // namespace mc
