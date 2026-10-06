#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <memory>
#include <functional>
#include <array>
#include <algorithm>

namespace sekiro::native {

struct FVector3 {
    float X{0.0f};
    float Y{0.0f};
    float Z{0.0f};

    constexpr FVector3() = default;
    constexpr FVector3(float x, float y, float z) : X(x), Y(y), Z(z) {}

    FVector3 operator+(const FVector3& o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
    FVector3 operator-(const FVector3& o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
    FVector3 operator*(float s) const { return {X * s, Y * s, Z * s}; }
    float Length() const { return std::sqrt(X * X + Y * Y + Z * Z); }
    float LengthSquared() const { return X * X + Y * Y + Z * Z; }
    FVector3 Normalized() const {
        float len = Length();
        return (len > 1e-6f) ? FVector3{X / len, Y / len, Z / len} : FVector3{0.f, 0.f, 0.f};
    }
};

struct FMatrix4 {
    float M[4][4]{
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1}
    };
};

enum class HavokCollisionLayer : uint32_t {
    Ground = 0,
    StaticMesh = 1,
    Character = 2,
    Hitbox = 3,
    Camera = 4
};

struct HavokHitResult {
    bool bHit{false};
    FVector3 HitPoint{};
    FVector3 HitNormal{};
    uint64_t HitEntityHandle{0};
    uint64_t HitColliderHandle{0};
    bool bIsStaticBlock{false};
};

class HavokStaticBoxCollider {
public:
    uint64_t Handle{0};
    FVector3 Center{};
    FVector3 HalfExtents{50.0f, 50.0f, 50.0f}; // 100cm cube
    bool bActive{true};
};

class SekiroVisualMeshComponent {
public:
    uint64_t Handle{0};
    FVector3 Position{};
    FVector3 RotationEuler{};
    FVector3 Scale{1.0f, 1.0f, 1.0f};
    std::string ModelName;
    int32_t CrackStage{-1};
    bool bVisible{true};
    float Alpha{1.0f};
};

class ChrIns {
public:
    uint64_t Handle{0};
    std::string Name{"Wolf"};
    uint32_t TeamId{0}; // 0 = Player, 1 = Enemy, 2 = Neutral
    bool bIsDead{false};
    float Health{100.0f};
    float MaxHealth{100.0f};
    float Posture{0.0f};         // 躯干值 (0 = Fresh, MaxPosture = Posture Broken)
    float MaxPosture{100.0f};
    bool bDeathblowReady{false}; // 忍杀红点就绪

    // Dantelion Model Alpha & Parts visibility
    bool bModelHidden{false};
    float ModelAlpha{1.0f};      // 0.0f = completely hidden
    bool bCapsulePhysicsActive{true}; // Havok physics capsule remains intact

    FVector3 Position{0.f, 0.f, 0.f};
    FVector3 Velocity{0.f, 0.f, 0.f};
    int32_t StaggerLevel{0};

    void ApplyImpulse(const FVector3& impulse) {
        Velocity = Velocity + impulse;
    }
};

class ChrCam {
public:
    FVector3 Position{0.f, 100.f, 0.f};
    FVector3 Forward{0.f, 0.f, 1.f};
    float Pitch{0.0f};
    float Yaw{0.0f};
    float Roll{0.0f};
    float Fov{70.0f};
};

class DantelionEngineContext {
public:
    using RaycastHandler = std::function<bool(
        const FVector3& start,
        const FVector3& end,
        HavokHitResult& outHit,
        uint64_t ignoreEntity
    )>;

    static inline RaycastHandler CustomRaycast{nullptr};

    static bool RaycastWorld(
        const FVector3& start,
        const FVector3& end,
        HavokHitResult& outHit,
        uint64_t ignoreEntity
    ) {
        if (CustomRaycast) {
            return CustomRaycast(start, end, outHit, ignoreEntity);
        }
        (void)start; (void)end; (void)outHit; (void)ignoreEntity;
        return false;
    }
};

} // namespace sekiro::native
