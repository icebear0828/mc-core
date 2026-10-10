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
    BlockTnt,
    ZombieSpawnEgg, // appended: the ids above stay as they are
    // The bow as it looks while it is drawn (the arrow nocked): view-model sprites chosen by the draw power, never an inventory item.
    BowPulling0,
    BowPulling1,
    BowPulling2
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

// Identity of a gameplay entity. Distinct from host pointers/handles and from block collider
// handles: nothing converts to it implicitly, so those namespaces cannot be mixed by accident.
// Adapters assign ids >= 2 to other entities; 0 means "no entity" and 1 is always the local player.
enum class EntityId : uint64_t {
    None = 0,
    LocalPlayer = 1,
};

struct RaycastResult {
    bool has_hit{false};
    Vec3 point{};
    Vec3 normal{};
    // Registered entity that was hit; None for terrain, unregistered actors and our own blocks.
    EntityId hit_entity{EntityId::None};
    // Collider handle (from createBlockCollider) when one of our voxel blocks was hit, else 0.
    uint64_t hit_collider_handle{0};
    // True for any block-like static hit (our voxel colliders or host static geometry).
    bool is_block{false};
};

} // namespace mc
