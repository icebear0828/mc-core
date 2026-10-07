# mc-core 接口契约实现规范 (Contracts Specification)

适配器工程必须完整实现 `mc-core` 暴露的四大核心纯虚接口。以下为各接口的责任边界与规范：

---

## 1. IPhysicsAdapter

```cpp
// 实体身份：强类型，禁止与宿主指针/句柄/碰撞体句柄混用。
// None(0) = 无实体；LocalPlayer(1) = 本地玩家（适配器在绑定玩家时自动注册）；其余实体由适配器分配 >= 2 的 id。
enum class EntityId : uint64_t { None = 0, LocalPlayer = 1 };

// RaycastResult：hit_entity 仅放已注册实体（地形/未注册 actor 为 None，禁止把宿主指针转成 id）；
// hit_collider_handle 仅放我们放置的方块碰撞体句柄。ignore_entity 需由适配器映射为宿主自己的句柄/Actor。
class IPhysicsAdapter {
public:
    virtual ~IPhysicsAdapter() = default;

    // 1. 世界视线与射击射线探测
    // 返回命中点坐标、表面法线以及命中的实体句柄
    virtual RaycastResult raycastWorld(const Vec3& start, const Vec3& end, EntityId ignore_entity = EntityId::None) = 0;

    // 2. 动态创建 1x1x1m 方块物理碰撞体
    // 必须确保宿主角色、NPC、载具能够踩踏站立和产生刚体阻挡
    virtual uint64_t createBlockCollider(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) = 0;

    // 3. 销毁方块物理碰撞体
    virtual void destroyBlockCollider(uint64_t collider_handle) = 0;

    // 4. 对物理实体施加线性冲量（用于 TNT 爆炸、三叉戟击退）
    virtual void applyLinearImpulse(EntityId entity_id, const Vec3& impulse) = 0;

    // 替换（非叠加）实体速度，MC 规范空间 cm/s；用于滑翔时驱动本地玩家。未知实体为空操作
    virtual void setLinearVelocity(EntityId entity_id, const Vec3& velocity) = 0;
};
```

---

## 2. IRenderAdapter

```cpp
class IRenderAdapter {
public:
    virtual ~IRenderAdapter() = default;

    // 1. 原生主角可见性控制
    // 变身时设为 false（隐藏网格，但保留物理胶囊体）；退出时设为 true
    virtual void setNativePlayerVisible(bool visible) = 0;

    // 2. 生成与销毁 12 个独立 Steve 刚体方块网格
    virtual bool spawnSteveParts() = 0;
    virtual void destroySteveParts() = 0;

    // 3. 每 Tick 同步 12 个方块部件的相对变换
    // transforms 数组包含纯数学计算好的相对旋转与位移
    virtual void updateStevePartTransforms(const SteveAnimator::PartTransforms& transforms) = 0;

    // 4. 手持物品视觉挂载
    virtual void setHeldItemVisual(ItemId item, bool is_offhand = false) = 0;

    // 5. 世界方块网格生成、裂纹阶段更新与销毁
    virtual uint64_t spawnBlockVisual(const GridPos& grid_pos, BlockId block_id, const Vec3& world_pos) = 0;
    virtual void setBlockCrackStage(uint64_t block_handle, int stage /* 0..9 */) = 0;
    virtual void destroyBlockVisual(uint64_t block_handle) = 0;
};
```

---

## 3. ICombatAdapter

```cpp
class ICombatAdapter {
public:
    virtual ~ICombatAdapter() = default;

    // 1. 将 MC 攻击意图转换为宿主原生状态变更（扣血、死亡、削韧）。
    //    禁止在此触发击退/硬直：CombatEngine::executeHit 在返回 true 后统一调用第 3 项，且只调用一次。
    virtual bool processHit(const HitIntent& intent) = 0;

    // 2. 查询目标最大生命值（用于 Boss 动态百分比平衡）
    virtual float getMaxHealth(EntityId entity_id) = 0;

    // 3. 触发宿主原生的受击硬直、趔趄或布娃娃（Ragdoll）；仅由核心调用，适配器不得在 processHit 内自行调用
    virtual void triggerStaggerOrRagdoll(EntityId entity_id, const Vec3& direction, float force) = 0;
};
```

---

## 4. IInputAdapter

```cpp
class IInputAdapter {
public:
    virtual ~IInputAdapter() = default;

    // 1. 检测玩家主手与副手手持物
    virtual ItemId getEquippedMainHand() const = 0;
    virtual ItemId getEquippedOffHand() const = 0;

    // 2. 读取玩家摄像机位置与朝向向量
    virtual Vec3 getCameraPosition() const = 0;
    virtual Vec3 getCameraForward() const = 0;

    // 3. 读取玩家世界坐标与当前移动速度（用于步态计算）
    virtual Vec3 getPlayerPosition() const = 0;
    virtual Vec3 getPlayerVelocity() const = 0;
};
```
