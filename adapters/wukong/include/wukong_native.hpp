#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <memory>
#include <functional>

namespace b1::native {

struct FVector {
    float X{0.0f};
    float Y{0.0f};
    float Z{0.0f};

    constexpr FVector() = default;
    constexpr FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

    FVector operator+(const FVector& o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
    FVector operator-(const FVector& o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
    FVector operator*(float s) const { return {X * s, Y * s, Z * s}; }
    float Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
    float SizeSquared() const { return X * X + Y * Y + Z * Z; }
    FVector GetSafeNormal() const {
        float s = Size();
        return (s > 1e-6f) ? FVector{X / s, Y / s, Z / s} : FVector{0.f, 0.f, 0.f};
    }
};

struct FRotator {
    float Pitch{0.0f};
    float Yaw{0.0f};
    float Roll{0.0f};

    constexpr FRotator() = default;
    constexpr FRotator(float p, float y, float r) : Pitch(p), Yaw(y), Roll(r) {}

    FVector Vector() const {
        float p = Pitch * (3.14159265f / 180.0f);
        float y = Yaw * (3.14159265f / 180.0f);
        float cp = std::cos(p);
        return {cp * std::cos(y), cp * std::sin(y), std::sin(p)};
    }
};

enum class ECollisionChannel : uint8_t {
    ECC_WorldStatic = 0,
    ECC_WorldDynamic = 1,
    ECC_Pawn = 2,
    ECC_Visibility = 3,
    ECC_Camera = 4,
    ECC_PhysicsBody = 5,
    ECC_Vehicle = 6,
    ECC_Destructible = 7
};

enum class ECollisionResponse : uint8_t {
    ECR_Ignore = 0,
    ECR_Overlap = 1,
    ECR_Block = 2
};

enum class ECollisionEnabled : uint8_t {
    NoCollision = 0,
    QueryOnly = 1,
    PhysicsOnly = 2,
    QueryAndPhysics = 3
};

enum class EAttachmentRule : uint8_t {
    KeepRelative = 0,
    KeepWorld = 1,
    SnapToTarget = 2
};

enum class EBGUAttrFloat : uint32_t {
    Hp = 1,
    HpMax = 2,
    Mp = 3,
    MpMax = 4,
    Stamina = 5,
    StaminaMax = 6
};

enum class ETraceTypeQuery : uint8_t {
    TraceTypeQuery1 = 0,
    TraceTypeQuery2 = 1
};

enum class EDrawDebugTrace : uint8_t {
    None = 0,
    ForOneFrame = 1,
    ForDuration = 2,
    Persistent = 3
};

class UObject {
public:
    virtual ~UObject() = default;
    std::string Name;
    bool bIsPendingKill{false};
};

class UActorComponent : public UObject {
public:
    virtual void BeginPlay() {}
    virtual void EndPlay() {}
};

class USceneComponent : public UActorComponent {
public:
    FVector RelativeLocation{0.f, 0.f, 0.f};
    FRotator RelativeRotation{0.f, 0.f, 0.f};
    FVector RelativeScale{1.f, 1.f, 1.f};
    USceneComponent* AttachParent{nullptr};
    std::vector<USceneComponent*> Children;
    bool bHiddenInGame{false};

    virtual void SetRelativeLocationAndRotation(const FVector& loc, const FRotator& rot) {
        RelativeLocation = loc;
        RelativeRotation = rot;
    }

    virtual void SetHiddenInGame(bool hide) {
        bHiddenInGame = hide;
    }

    virtual void AttachToComponent(USceneComponent* parent, EAttachmentRule rule = EAttachmentRule::KeepRelative) {
        (void)rule;
        AttachParent = parent;
        if (parent) {
            parent->Children.push_back(this);
        }
    }
};

class UPrimitiveComponent : public USceneComponent {
public:
    ECollisionEnabled CollisionEnabled{ECollisionEnabled::QueryAndPhysics};
    ECollisionChannel CollisionObjectType{ECollisionChannel::ECC_WorldDynamic};
    ECollisionResponse CollisionResponse{ECollisionResponse::ECR_Block};
    FVector LinearImpulse{0.f, 0.f, 0.f};

    virtual void SetCollisionEnabled(ECollisionEnabled enabled) { CollisionEnabled = enabled; }
    virtual void SetCollisionObjectType(ECollisionChannel channel) { CollisionObjectType = channel; }
    virtual void SetCollisionResponseToAllChannels(ECollisionResponse response) { CollisionResponse = response; }
    virtual void AddImpulse(const FVector& impulse) { LinearImpulse = LinearImpulse + impulse; }
};

class UCapsuleComponent : public UPrimitiveComponent {
public:
    float CapsuleHalfHeight{96.0f};
    float CapsuleRadius{42.0f};
    float GetScaledCapsuleHalfHeight() const { return CapsuleHalfHeight; }
};

class UBoxComponent : public UPrimitiveComponent {
public:
    FVector BoxExtent{50.0f, 50.0f, 50.0f};
    void SetBoxExtent(const FVector& extent) { BoxExtent = extent; }
};

class UProceduralMeshComponent : public UPrimitiveComponent {
public:
    int32_t CrackStage{-1};
    std::string CurrentMeshModel;
    void SetMeshSection(int32_t sectionIndex, const std::string& modelName) {
        (void)sectionIndex;
        CurrentMeshModel = modelName;
    }
};

class AActor : public UObject {
public:
    USceneComponent* RootComponent{nullptr};
    FVector ActorLocation{0.f, 0.f, 0.f};
    FRotator ActorRotation{0.f, 0.f, 0.f};
    bool bIsDestroyed{false};

    virtual FVector GetActorLocation() const { return ActorLocation; }
    virtual void SetActorLocation(const FVector& loc) { ActorLocation = loc; }
    virtual FRotator GetActorRotation() const { return ActorRotation; }
    virtual void SetActorRotation(const FRotator& rot) { ActorRotation = rot; }
    virtual void Destroy() { bIsDestroyed = true; }
    virtual bool IsDestroyed() const { return bIsDestroyed; }
};

struct FHitResult {
    bool bBlockingHit{false};
    FVector ImpactPoint{};
    FVector ImpactNormal{};
    AActor* Actor{nullptr};
    UPrimitiveComponent* Component{nullptr};
};

class APawn : public AActor {};

class ACharacter : public APawn {
public:
    UCapsuleComponent* CapsuleComponent{nullptr};
    USceneComponent* MeshComponent{nullptr};
    FVector Velocity{0.f, 0.f, 0.f};

    UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
    USceneComponent* GetMesh() const { return MeshComponent; }
    FVector GetVelocity() const { return Velocity; }
    void LaunchCharacter(const FVector& launchVelocity, bool bXYOverride, bool bZOverride) {
        // UE5 semantics: an override flag replaces that component, otherwise it is added
        Velocity.X = bXYOverride ? launchVelocity.X : Velocity.X + launchVelocity.X;
        Velocity.Y = bXYOverride ? launchVelocity.Y : Velocity.Y + launchVelocity.Y;
        Velocity.Z = bZOverride ? launchVelocity.Z : Velocity.Z + launchVelocity.Z;
    }
};

class ABGUCharacter : public ACharacter {
public:
    uint32_t TeamId{1};
    bool bIsDead{false};
    float Health{100.0f};
    float MaxHealth{100.0f};
    int32_t LastStaggerLevel{0};
};

class ABGUPlayerCharacter : public ABGUCharacter {
public:
    ABGUPlayerCharacter() {
        TeamId = 0; // Player team
    }
};

class APlayerCameraManager : public AActor {
public:
    FVector CameraLocation{0.f, 0.f, 0.f};
    FRotator CameraRotation{0.f, 0.f, 0.f};
    FVector GetCameraLocation() const { return CameraLocation; }
    FRotator GetCameraRotation() const { return CameraRotation; }
};

class APlayerController : public AActor {
public:
    APlayerCameraManager* PlayerCameraManager{nullptr};
    ABGUPlayerCharacter* ControlledPawn{nullptr};

    ABGUPlayerCharacter* GetControlledPawn() const { return ControlledPawn; }
    APlayerCameraManager* GetPlayerCameraManager() const { return PlayerCameraManager; }
};

class UWorld : public UObject {};

class UBGUFunctionLibrary {
public:
    static bool BGUIsUnitDead(ABGUCharacter* unit) {
        return unit ? unit->bIsDead : true;
    }

    static bool BGUIsEnemyTeam(ABGUCharacter* a, ABGUCharacter* b) {
        if (!a || !b) return false;
        return a->TeamId != b->TeamId;
    }

    static float BGUGetFloatAttr(ABGUCharacter* unit, EBGUAttrFloat attr) {
        if (!unit) return 0.0f;
        if (attr == EBGUAttrFloat::Hp) return unit->Health;
        if (attr == EBGUAttrFloat::HpMax) return unit->MaxHealth;
        return 0.0f;
    }

    static void BGUSetFloatAttr(ABGUCharacter* unit, EBGUAttrFloat attr, float val) {
        if (!unit) return;
        if (attr == EBGUAttrFloat::Hp) unit->Health = val;
        if (attr == EBGUAttrFloat::HpMax) unit->MaxHealth = val;
    }

    static AActor* BGUSpawnActor(UWorld* world, const FVector& location, const FRotator& rotation) {
        (void)world;
        auto* actor = new AActor();
        actor->ActorLocation = location;
        actor->ActorRotation = rotation;
        return actor;
    }

    static void BGUApplyImpulse(ABGUCharacter* unit, const FVector& impulse) {
        if (unit) {
            unit->LaunchCharacter(impulse, false, false);
        }
    }
};

class UGSE_EngineFuncLib {
public:
    static void RegisterComponent(UActorComponent* component) {
        if (component) {
            component->BeginPlay();
        }
    }
    static void UnregisterComponent(UActorComponent* component) {
        if (component) {
            component->EndPlay();
        }
    }
};

class UKismetSystemLibrary {
public:
    using LineTraceHandler = std::function<bool(
        UObject* worldContext,
        const FVector& start,
        const FVector& end,
        ETraceTypeQuery traceChannel,
        bool bTraceComplex,
        const std::vector<AActor*>& actorsToIgnore,
        EDrawDebugTrace drawDebugType,
        FHitResult& outHit,
        bool bIgnoreSelf
    )>;

    static inline LineTraceHandler CustomLineTrace{nullptr};

    static bool LineTraceSingle(
        UObject* worldContext,
        const FVector& start,
        const FVector& end,
        ETraceTypeQuery traceChannel,
        bool bTraceComplex,
        const std::vector<AActor*>& actorsToIgnore,
        EDrawDebugTrace drawDebugType,
        FHitResult& outHit,
        bool bIgnoreSelf
    ) {
        if (CustomLineTrace) {
            return CustomLineTrace(worldContext, start, end, traceChannel, bTraceComplex, actorsToIgnore, drawDebugType, outHit, bIgnoreSelf);
        }
        (void)worldContext; (void)start; (void)end; (void)traceChannel;
        (void)bTraceComplex; (void)actorsToIgnore; (void)drawDebugType;
        (void)outHit; (void)bIgnoreSelf;
        return false;
    }
};

} // namespace b1::native
