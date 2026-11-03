# Minecraft 1.21.8 鞘翅 / 烟花 / 末影珍珠 逆向记录

本文是 `mc-core` 实现鞘翅滑翔与末影珍珠的**事实来源**。只记录结论、公式和验证方法，不收录反编译源码。

- 目标版本：Minecraft Java Edition **1.21.8**
- 客户端 jar：`D:\game\sekiro\build\mc_client_1.21.8.jar`
  - size `29,525,242`
  - SHA1 `a19d9badbea944a4369fd0059e53bf7286597576`（与 Mojang 1.21.8 manifest 的 `downloads.client.sha1` 一致）
- 官方 mappings：`client.txt`，SHA1 `bdeb624c3aefba11d9d40f34bc96176350b549b6`
- 证据等级：**A（官方 mappings + 反编译源码，逐方法核对）**。尚未做原版实机 trace；第 8 节给出补 trace 的方法。

## 0. 工具链（可复现）

反编译产物**不在仓库内**，放在 `C:\Users\30435\mc-re\`：

```text
mc-re/
  1.21.8/
    client.txt              # Mojang official mappings (ProGuard)
    client.srg              # 由 client.txt 转成的 SRG
    client-mapped.jar       # SpecialSource 重映射后的 jar
    src/                    # Vineflower 反编译结果
  tools/
    proguard_to_srg.py      # ProGuard -> SRG（含 descriptor 类型名替换）
    SpecialSource-1.11.6-shaded.jar
    vineflower-1.12.0.jar
```

流程：

1. 从 Mojang manifest 取 `1.21.8.json`，下载 `client_mappings`（即 `client.txt`）。
2. `proguard_to_srg.py client.txt client.srg`。
3. `java -jar SpecialSource-1.11.6-shaded.jar -i client.jar -m client.srg -o client-mapped.jar`。
4. `java -jar vineflower-1.12.0.jar --only=<class> client-mapped.jar src`。

> 规则：反编译源码和官方 mappings 都不提交。本文只写公式与结论。

## 1. 单位与坐标系

- 原版：**1 tick = 1/20 s**；速度单位 **block/tick**；坐标 **y 向上**。
- `xRot`（俯仰）单位为度，**正数 = 向下看**，范围 `[-90, 90]`。
- `yRot`（偏航）单位为度，`0` 朝 `+Z`，`90` 朝 `-X`。
- 视线单位向量（`Entity.calculateViewVector` / `getLookAngle`）：

```text
look.x = -sin(yRot) * cos(xRot)
look.y = -sin(xRot)
look.z =  cos(yRot) * cos(xRot)
```

- 从归一化相机前向 `f` 反推：

```text
xRot = -asin(clamp(f.y, -1, 1)) * 180/pi     // 正数向下
yRot = atan2(-f.x, f.z) * 180/pi
hLook = hypot(f.x, f.z)                       // 等于 cos(xRot)
```

- `mc-core` / 法环适配器的世界坐标是 **y 向上**（`mc/flight.hpp` 的 `pos[1]` 就是高度，`CameraPose.forward` 也是 y-up）。
- **当前 `mc::ElytraFlight` 用的是 z-up**（`state_.velocity.z -= gravity * dt`，测试里也把 `z` 当高度）。这是需要修正的坐标约定，不能直接把本文公式套进去。

## 2. 状态机

### 2.1 开始滑翔

`Player.tryToStartFallFlying()`：

```text
if (!isFallFlying() && canGlide() && !isInWater()) {
    startFallFlying();   // 置 shared flag 7
    return true;
}
```

`canGlide()`：

```text
!onGround()
&& !isPassenger()
&& !hasEffect(LEVITATION)
&& 任意装备槽存在可滑翔物品
```

`canGlideUsing(stack, slot)`：

```text
stack 有 GLIDER 组件
&& stack 的 EQUIPPABLE 槽位 == slot
&& !stack.nextDamageWillBreak()
```

`Player.canGlide()` 额外要求 `!abilities.flying`（创造飞行不能同时滑翔）。

原版触发点（`LocalPlayer.aiStep`）：

```text
jump 当前按下
&& 本 tick 开始时 jump 未按下        // 新按下的沿，不是持续按住
&& 跳跃没有被创造飞行双击切换消费
&& !onClimbable()
&& tryToStartFallFlying()
    -> 客户端发 START_FALL_FLYING
    -> 服务端 ServerGamePacketListenerImpl 再调一次 tryToStartFallFlying()
       失败则 stopFallFlying()
```

适配器可以用“跳跃键按下沿 + 空中 + canGlide”作为触发条件；注意创造飞行的双击跳跃会先消费这次按键，不应再触发滑翔。

### 2.2 停止滑翔

`stopFallFlying()` 只做一件事：置位再清位 shared flag 7（让客户端收到状态变化）。

实际停止条件：

- `updateFallFlying()` 每 tick 检查 `!canGlide()` → 清 flag。落地、成为乘客、获得 levitation、装备损坏/卸下都会停。
- `travelFallFlying()` 里如果 `onClimbable()` → 走 `travelInAir()` 并 `stopFallFlying()`。
- `travel()` 的优先级是：**流体 → 鞘翅 → 普通空中**。在水/岩浆里走 `travelInFluid()`，鞘翅物理不生效。
- 进入水中时 `tryToStartFallFlying()` 会拒绝；已经在滑翔时进入水中，`travel` 改走流体分支，flag 可能暂时还在，但不再有滑翔物理。

### 2.3 耐久与计时

`updateFallFlying()` 服务端逻辑：

- `fallFlyTicks++`
- 每 10 tick 发一次 `ELYTRA_GLIDE` game event
- 每 20 tick（`(ticks/10) % 2 == 0`）随机选一个可滑翔装备槽，扣 1 点耐久

适配器如果做生存模式，需要把这两条接进物品耐久；做创造/演示可以先不实现，但状态机里要保留 `fallFlyTicks`。

## 3. 鞘翅每 tick 物理（精确顺序）

`LivingEntity.travelFallFlying()`：

```text
if (onClimbable()) {
    travelInAir(input);
    stopFallFlying();
    return;
}

v_before = getDeltaMovement();
h_before = horizontalDistance(v_before);
setDeltaMovement(updateFallFlyingMovement(v_before));
move(MoverType.SELF, getDeltaMovement());      // 碰撞在这里发生
if (serverSide) {
    h_after = horizontalDistance(getDeltaMovement());
    handleFallFlyingCollisions(h_before, h_after);
}
```

**关键：`travelFallFlying` 完全忽略 `travel(input)` 的 input。** 滑翔时 WASD 不产生移动，只有鼠标视角参与物理。

`updateFallFlyingMovement(v)`，`v` 单位 block/tick，`y` 向上：

```text
look    = getLookAngle()                 // 单位向量
pitch   = xRot * pi/180                  // 正数向下
hLook   = hypot(look.x, look.z)          // = cos(pitch)
hVel    = hypot(v.x, v.z)                // 本 tick 开始时的水平速度
g       = getEffectiveGravity()          // 默认 0.08

cos2 = cos(pitch)^2

// 1. 重力：平飞时只保留 25%，俯冲时接近全重力
v.y += g * (-1 + cos2 * 0.75)

// 2. 俯冲/下沉把垂直速度转成沿视线方向的速度
if (v.y < 0 && hLook > 0) {
    d = v.y * -0.1 * cos2
    v.x += look.x * d / hLook
    v.y += d
    v.z += look.z * d / hLook
}

// 3. 抬头产生升力（xRot < 0 = 向上看）
if (pitch < 0 && hLook > 0) {
    d = hVel * -sin(pitch) * 0.04
    v.x += -look.x * d / hLook
    v.y += d * 3.2
    v.z += -look.z * d / hLook
}

// 4. 水平速度朝“当前视线方向的水平分量”收敛
if (hLook > 0) {
    v.x += (look.x / hLook * hVel - v.x) * 0.1
    v.z += (look.z / hLook * hVel - v.z) * 0.1
}

// 5. 阻力（水平 0.99，垂直 0.98）
v.x *= 0.99
v.y *= 0.98
v.z *= 0.99
```

注意顺序：第 2 步用的是**加完重力后的 `v.y`**；第 3、4 步用的是**本 tick 开始时的 `hVel`**；阻力最后乘。

### 3.1 重力

```text
getDefaultGravity() = GRAVITY 属性值（玩家默认 0.08）
getGravity()        = isNoGravity() ? 0 : getDefaultGravity()
getEffectiveGravity():
    falling = getDeltaMovement().y <= 0
    return (falling && hasEffect(SLOW_FALLING)) ? min(getGravity(), 0.01) : getGravity()
```

### 3.2 碰撞与撞墙伤害

`move(MoverType.SELF, delta)` 的行为（`Entity.move`）：

- 用 `collide(delta)` 得到实际位移。
- 如果 x 或 z 的实际位移与意图不同，`horizontalCollision = true`。
- 碰撞后把撞墙轴的速度分量清零：

```text
if (horizontalCollision) {
    if (x 被挡住) v.x = 0
    if (z 被挡住) v.z = 0
}
```

`handleFallFlyingCollisions(h_before, h_after)`（仅服务端）：

```text
if (horizontalCollision) {
    lost = h_before - h_after
    damage = lost * 10 - 3
    if (damage > 0) {
        playSound(fallDamageSound(damage))
        hurt(flyIntoWall, damage)
    }
}
```

**伤害按“损失的水平速度”算，不是按撞击法线速度算。** `h_before` 是 `move` 前，`h_after` 是 `move` 后（碰撞轴已被清零）。伤害单位是半颗心（1.0 = 半颗心），例如水平速度从 1.5 掉到 0，`damage = 12`，即 6 颗心。

### 3.3 没有最大速度上限

原版鞘翅没有 `max_speed` 常量。终端速度由重力/阻力/视线角度共同决定。不要保留当前 `ElytraConfig::max_speed = 3500 cm/s` 的硬截断。

### 3.4 数值验证（按上述公式逐 tick 模拟）

从静止开始，20 tick/s，`g = 0.08`，收敛后：

| 视线 | 速度 (block/tick) | 速度 (m/s) | 垂直速度 (m/s) |
|---|---|---|---|
| 平飞（pitch 0） | `(0, -0.1495, 1.5102)` | ~30.3 | -3.0 |
| 俯冲 45°（pitch +45） | `(0, -0.6746, 3.2283)` | ~66.0 | -13.5 |
| 垂直俯冲（pitch +90） | `(0, -3.9188, 0)` | ~78.4 | -78.4 |
| 抬头 45°、初速 0 | `(0, -0.2259, 0.3491)` | ~8.3 | -4.5 |
| 平飞 + Slow Falling (`g=0.01`) | `(0, -0.0187, 0.1851)` | ~3.7 | -0.4 |
| 烟花速度目标（30 tick，不含滑翔项） | `(0, 0, 1.70)` | ~34.0 | 0 |

抬头不是无条件爬升：从静止抬头 45° 仍会缓慢下沉；**先有水平速度**才能把速度换成高度。例如初速 `1.5 block/tick`、抬头 45°，10 tick 后 `v.y = +0.59`（约 +11.8 m/s），确实在爬升。

## 4. 鼠标转向（跟随视线）

### 4.1 WASD 无效

`travelFallFlying` 不使用 `travel` 的 input。滑翔中的方向完全由 `getLookAngle()` 决定。适配器在滑翔状态下应**忽略 WASD 位移**，只读相机 yaw/pitch。

### 4.2 水平转向速度

第 4 步把水平速度向量朝 `hVel * look_horizontal` 收敛，系数 `0.1`。设当前速度与目标方向夹角为 θ，单 tick 后方向变化约为：

```text
Δθ ≈ atan(0.1 / 0.9) ≈ 6.34°
```

与速度大小无关。也就是说：

- 90° 转向大约需要 **15–20 tick（0.75–1.0 s）** 才能基本对准。
- 速度越快，转弯半径越大（因为方向转得一样快，但线速度更大）。
- 这是原版的“滑翔惯性”，不要做成瞬间转向。

实测模拟（初速 `1.5 block/tick`，视线从 +Z 突然转到 -X）：

| tick | v (block/tick) | 与 +Z 的夹角 |
|---|---|---|
| 1 | `(-0.150, -0.018, 1.337)` | ~6.4° |
| 5 | `(-0.506, -0.070, 0.842)` | ~31° |
| 10 | `(-0.680, -0.107, 0.473)` | ~55° |
| 20 | `(-0.797, -0.137, 0.149)` | ~79° |

### 4.3 俯仰

- `xRot < 0`（抬头）：第 3 步产生升力，升力与本 tick 开始时的水平速度 `hVel` 成正比。
- `xRot > 0`（低头）：`cos2` 变小，重力项接近 `-g`；第 2 步把下沉速度转成沿视线方向的速度，所以俯冲会加速。
- `xRot = ±90` 时 `hLook = 0`，第 2/3/4 步全部跳过，只剩重力 + 阻力，垂直终端约 `-3.92 block/tick`（-78.4 m/s）。

## 5. 烟花加速

### 5.1 生成条件

`FireworkRocketItem.use`：

```text
if (!player.isFallFlying()) return PASS;
spawnProjectile(new FireworkRocketEntity(level, stack, player), ...);
stack.consume(1, player);
```

只有**正在滑翔**时使用烟花才会生成“附着在玩家身上”的火箭。否则是普通烟花（`useOn` 放置/发射）。

### 5.2 每 tick 推进

`FireworkRocketEntity.tick()`，当 `attachedToEntity != null` 且 `attachedToEntity.isFallFlying()`：

```text
look = owner.getLookAngle()
v    = owner.getDeltaMovement()

v.x = v.x + look.x * 0.1 + (look.x * 1.5 - v.x) * 0.5
v.y = v.y + look.y * 0.1 + (look.y * 1.5 - v.y) * 0.5
v.z = v.z + look.z * 0.1 + (look.z * 1.5 - v.z) * 0.5

owner.setDeltaMovement(v)
```

等价于：

```text
v' = 0.5 * v + 0.85 * look
```

- 这是一个**速度目标**，不是恒定加速度。终端 `v = 1.7 * look`（block/tick），即 **34 m/s** 沿视线方向。
- 火箭的 `deltaMovement` 同步设为玩家的 `deltaMovement`，位置跟随玩家（加手持偏移）。
- 如果玩家停止滑翔，`isFallFlying()` 为 false，**不再加速**，火箭只跟随。

### 5.3 持续时间

`FireworkRocketEntity` 构造：

```text
flightDuration = Fireworks 组件的 flight_duration（默认 0；可合成 1/2/3）
lifetime = 10 * (1 + flightDuration) + randInt(6) + randInt(7)   // tick
```

| flight_duration | lifetime 值 (tick) | 实际推进 tick 数 | 时间 |
|---|---|---|---|
| 0 | 10–21 | 11–22 | 0.55–1.10 s |
| 1 | 20–31 | 21–32 | 1.05–1.60 s |
| 2 | 30–41 | 31–42 | 1.55–2.10 s |
| 3 | 40–51 | 41–52 | 2.05–2.60 s |

`life` 从 0 开始，每 tick 先应用加速再 `life++`；`life > lifetime` 时爆炸并 `discard()`。所以实际加速次数是 `lifetime + 1`。命中方块/实体也会提前爆炸（`hasExplosion()` 时）。

### 5.4 tick 顺序

玩家和火箭是两个实体。火箭在玩家之后（同 tick 或下一 tick）tick，写入的是玩家的 `deltaMovement`，因此**影响的是下一 tick 的滑翔物理**。适配器里应在滑翔步进的**末尾**（或下一 tick 开头）应用烟花速度 lerp，而不是在 `updateFallFlyingMovement` 中间。

## 6. 末影珍珠

### 6.1 投掷

`EnderpearlItem.use`：

```text
spawnProjectileFromRotation(ThrownEnderpearl::new, level, stack, player,
                            xRotOffset = 0.0,
                            power = 1.5,
                            uncertainty = 1.0);
```

- 初始位置：`owner.getX(), owner.getEyeY() - 0.1, owner.getZ()`。
- 初速方向：由玩家 `xRot/yRot` 得到单位向量。
- 随机扰动：每轴加 `triangle(0, 0.0172275 * uncertainty)`，`uncertainty = 1.0`。
- 再乘 `power = 1.5`（block/tick，即 **30 m/s**）。
- 继承玩家速度：`shootFromRotation` 最后把 `owner.getKnownMovement()` 加上去；**x/z 总是加，y 只在玩家不在地面时加**。

### 6.2 飞行

`ThrowableProjectile.tick()`：

```text
v.y -= 0.03            // getDefaultGravity()
v *= 0.99              // 水中 0.8
沿 v 做碰撞检测并移动
```

- 重力 `0.03 block/tick²` = **12 m/s²**，不是 9.8。
- 阻力 `0.99`/tick（水中 `0.8`）。
- 没有原版寿命上限；珍珠只在命中、区块卸载、掉出世界、或 owner 死亡且游戏规则 `ENDER_PEARLS_VANISH_ON_DEATH`（默认 true）时消失。

### 6.3 命中与传送

`ThrownEnderpearl.onHit`（仅服务端、且珍珠未被移除）：

```text
owner = getOwner()
if (owner != null && isAllowedToTeleportOwner(owner, level)) {
    target = oldPosition()          // 本 tick 开始时的珍珠位置
    if (owner instanceof ServerPlayer) {
        owner.teleport(target, relative = ROTATION | DELTA)
        owner.resetFallDistance()
        owner.resetCurrentImpulseContext()
        owner.hurtServer(level, enderPearl, 5.0)
        playSound(PLAYER_TELEPORT, target)
    } else {
        owner.teleport(target, relative = ROTATION | DELTA)
        owner.resetFallDistance()
    }
}
discard()                           // 无论是否传送成功都丢弃珍珠
```

要点：

- 传送目标不是命中点，而是 `oldPosition()`。`ServerLevel.tickNonPassenger` 在 `entity.tick()` **之前**调用 `setOldPosAndRot()`，所以 `oldPosition` 是**本 tick 开始时的位置**，比命中点晚一 tick 的行程（约 1.5 block/tick × 0.99 ≈ 1.49 block，实际还受重力影响）。
- 保留 owner 的朝向和速度（`Relative.ROTATION | DELTA`）；`DO_NOTHING` 后置处理。
- 传送后立刻 `resetFallDistance()`，所以**不会**因为传送前的下落而摔伤；随后固定受 **5.0 点末影珍珠伤害**（2.5 颗心），走正常伤害规则（创造/无敌可能免疫）。
- `isAllowedToTeleportOwner`：
  - 同维度：owner 存活且不在睡觉。
  - 跨维度：`owner.canUsePortal(true)`。
- `canTeleport` 额外限制：从 End 到 Overworld 需要 `seenCredits`。
- `onHitEntity` 只对命中实体造成 **0 伤害**（用于命中判定/音效），不产生额外伤害。
- 珍珠注册区块 ticket（`registerAndUpdateEnderPearlTicket`）是为了让目标区块保持加载；单机常驻世界不需要复刻。

## 7. 与当前 `mc-core` 实现的差异

| 项 | 当前实现 | 原版 1.21.8 | 处理 |
|---|---|---|---|
| 坐标 | `ElytraFlight` 用 z-up | y-up | 重写为 y-up；`Vec3` 本身无约定，但适配器/`flight.hpp` 都是 y-up |
| 重力 | `ElytraConfig::gravity = 980 cm/s²`（9.8 m/s²） | `0.08 block/tick²` = 3200 cm/s²（32 m/s²） | 改为按 tick 的 `g = 0.08` |
| 垂直阻力 | 0.99 | 0.98 | 分开 x/z 与 y |
| 最大速度 | `max_speed = 3500 cm/s` 硬截断 | 无上限 | 删除截断 |
| 烟花 | 恒定加速度 `boost_acceleration = 2200 cm/s²`，按秒计时 | 速度 lerp `v' = 0.5v + 0.85 look`，按 tick 计时，20–51 tick | 改为速度目标 + tick 计时 |
| 鼠标转向 | 只用 yaw 算 boost 方向，滑翔本身不转向 | 水平速度每 tick 朝视线方向收敛 10% | 加入第 3 节第 4 步 |
| 俯仰 | 自拟 lift/dive 公式 | 第 2/3 步的精确公式 | 替换 |
| 起始条件 | 只检查 `on_ground` | `canGlide()`：空中、非乘客、无 levitation、有 GLIDER 装备、非创造飞行、不在水 | 补全 |
| 撞墙伤害 | 按法线撞击速度 + 阈值 | 按损失的水平速度 `(h_before-h_after)*10-3` | 替换 |
| 步进 | 按帧 `dt` 直接积分 | 20 tick/s 的固定步进 | 用 20 Hz 累加器 |
| 末影珍珠重力 | `980 cm/s²` | `0.03 block/tick²` = 1200 cm/s² | 改为 1200（或按 tick 0.03） |
| 末影珍珠传送 | 无 | 命中后传送 owner 到 `oldPosition()`，5.0 伤害，discard | 新增 |
| 末影珍珠初速 | 3000 cm/s | 1.5 block/tick = 3000 cm/s | 数值已一致，补 owner 速度继承与随机扰动 |

## 8. 适配器落地与验证计划

### 8.1 纯逻辑层

建议在 `mc-core/include/mc/elytra.hpp` / `src/elytra.cpp` 重写为：

- 输入：`Vec3 velocity`（block/tick，y-up）、`Vec3 look`（单位向量）、`double gravity`、`bool slow_falling`。
- 输出：`Vec3 new_velocity`（block/tick）。
- 另一个函数做碰撞后的速度修正与伤害计算。
- 状态机：`is_gliding`、`fall_fly_ticks`、`firework_ticks_left`、`firework_flight_duration`。
- 所有常量用 `block/tick`，不要再混 cm/s；适配器写位置时再乘 100 或按现有 `layout` 单位转换。

### 8.2 适配器层

- 用 20 Hz 固定步进累加器跑鞘翅/珍珠；渲染帧只做插值。
- 视角：`CameraPose.forward` → `xRot/yRot`（第 1 节公式）。
- 滑翔时忽略 WASD；跳跃键按下沿 + 空中 + canGlide → 开始滑翔。
- 位置写入复用现有路径：`PhysicsModule + kPhysicsPosition`，并置 `+0x91` proxy sync（`stepFlight` / `stepOwRocketPunch` 已验证）。
- 滑翔期间把 fall timer 清零（同 `stepFlight`），否则法环的 12 s 滞空击杀会误杀。
- 碰撞：用 `CastStaticRay` 在脚/腰/胸多个高度做 sweep；水平碰撞时清零对应轴速度，并按第 3.2 节算伤害。
- 烟花：使用烟花物品且正在滑翔时，启动 `firework_ticks_left`；每 tick 应用速度 lerp；播放原版音效/粒子。
- 珍珠：使用珍珠时生成一个轻量 projectile（位置/速度/owner 速度），20 Hz 步进 + 射线/实体命中；命中后把玩家位置写到 `oldPosition`，清 fall timer，入队 5.0 伤害，discard，播音效/粒子。

### 8.3 测试

1. **公式单测**：把第 3 节的递推写成测试，覆盖平飞、45° 俯冲、90° 俯冲、抬头、Slow Falling、烟花 30 tick 的终端值（见 3.4 表）。
2. **转向单测**：初速 1.5、视线 90° 突变，断言 1/5/10/20 tick 的速度方向（见 4.2 表）。
3. **撞墙伤害**：`h_before=1.5, h_after=0` → `damage=12`；`h_before=0.2, h_after=0` → `damage=-1`（不受伤）。
4. **珍珠**：初速 1.5、重力 0.03、阻力 0.99，命中后目标 = 上一 tick 位置；5.0 伤害；discard。
5. **实机 oracle（下一步）**：录一段原版 1.21.8 的滑翔 trace（每 tick 的 position/velocity/xRot/yRot/firework 状态），用 `reverse-engineering` 的 replay 方法对比。当前只有源码级证据（A），trace 是最终验收。

### 8.4 末影珍珠（2026-11-04 落地）

- 纯逻辑：`include/mc/ender_pearl.hpp` / `src/ender_pearl.cpp`，单测 `tests/test_ender_pearl.cpp`。
  - `launchVelocity()`：`normalize(look) + triangle(0, 0.0172275)` 每轴，再乘 1.5；最后加 owner 速度（x/z 总是加，y 只在不在地面时加）。
  - `beginTick()`：`v.y -= 0.03`，`v *= 0.99`（水中 0.8），`prev_position = position`；适配器每帧按 `dt/0.05` 积分并用 `CastStaticRay` 解算碰撞。
  - 纯逻辑的 `teleportTarget()` 仍是 `prev_position`（原版 `oldPosition()`），但适配器不用它：20 Hz tick 的位置最多落后 1.5 格，实机会看起来"落点外"。适配器改用命中点沿法线外推（地面 0.05 m、墙面 0.45 m）。不再做向下射线吸附：那会把墙面/屋檐命中抬到上一层地面，造成落点偏移；穿地已由子步进射线解决。
- 适配器：`PlayerLocomotion::stepEnderPearl()`，在 `on_camera_step_pre` 里排在 `ElytraStep` 之后。
  - 右键且手持末影珍珠 → `TryThrowEnderPearl()` 置请求，下一步在相机线程读位置/视线/owner 速度后发射。
  - 每帧位移按 ≤0.5 m 子步进做射线，防止掉帧时高速珍珠穿过薄地面。
  - 引擎浮动原点重基只按 8 m 整数格平移珍珠；非整数格的玩家传送/加载直接丢弃珍珠，避免把珍珠（和落点）整体带偏。**hold 期间也要把传送目标一起平移到新坐标系**：否则 hold 每帧写回旧坐标、引擎每帧重基一次，玩家会来回"瞬移"40 m（2026-11-04 实机日志确认）。hold 的第一帧不做重基判断——那时读到的还是传送前的位置，传送距离本身（如 24/-8）可能被误判成 8 m 整数格，把目标平移几十米。
  - 传送后约 200 ms 内看门狗：如果引擎把角色拉回 >2 m，自动再压 1–2 次，保证传送"粘住"。
  - 命中后把目标位置**连续写若干帧**（最多 15 帧，读到误差 <0.5 m 且至少 6 帧后释放），同时写 `+0x91` proxy sync、清 fall timer；这与创造飞行/鞘翅的写入方式一致，一次性写会被引擎拉回。
  - 传送时停掉鞘翅、把创造飞行的 `fly_own_` 同步到目标；按 `scaleToEr(5.0, max_hp)` 扣血（原版 5.0 = 25% 最大生命）；播放 `entity.player.teleport`，丢弃珍珠。
- 渲染：`BuildPearlMesh()`（`render/steve_mesh_factory.cpp`）用 HUD 图集里的原版珍珠 sprite 生成单个相机朝向的 billboard 四边形（世界矩阵取相机 right/up，像原版物品实体），`SteveRenderer::drawPearls()` 在世界空间绘制；不用箭矢的 Spark 粒子。
- 音效：投掷用 `entity.ender_eye.launch`（末影之眼 whoosh），命中用 `entity.player.teleport`。原版 `entity.ender_pearl.throw` 就是 `random/bow`（和箭矢音同一文件），演示里听起来像射箭，所以不用它；`tools/extract_mc_sounds.py` 的 `EVENTS` 仍保留它，便于以后按需恢复。
- 未做：实体命中（只做了静态世界射线）、跨维度限制、末影螨 5% 概率、原版 portal 粒子。

### 8.5 三叉戟激流（2026-11-04）

原版 1.21.8（`TridentItem.releaseUsing` + `Player.startAutoSpinAttack` + `LivingEntity.travelInAir` + `data/minecraft/enchantment/riptide.json`）：

- 蓄力 10 tick（`THROW_THRESHOLD_TIME`）；激流强度 I/II/III = **1.5 / 2.25 / 3.0** block/tick（base 1.5，每级 +0.75）。
- `push(normalize(look) * strength)`，然后 `startAutoSpinAttack(20 tick, 8.0 伤害, trident)`；落地释放时先 `move(0, 1.2, 0)` 抬升。
- 普通空中移动：水平阻力 0.91、垂直 0.98、重力 0.08（block/tick²）。
- 旋转期间碰到实体会造成 8.0 伤害并结束旋转；水平撞墙也会结束。
- 音效按等级 `item.trident.riptide_1/2/3`；需要 `isInWaterOrRain`。

适配器实现：

- 纯逻辑 `include/mc/riptide.hpp` / `src/riptide.cpp`，单测 `tests/test_riptide.cpp`。
- `PlayerLocomotion::stepRiptide`：释放请求在相机线程消费；20 tick 内每帧写位置（≤0.5m 子步进射线、跟随 8m 重基），撞墙/引擎传送即停；同时驱动 `steve.riptide`（旋转动画 + 水花粒子）和 `steve.trident_charge`（蓄力动画）。
- 输入：手持三叉戟按住右键蓄力 10 tick，松开调用 `TryStartRiptide`；`mc_er_steve.txt` 的 `riptide_level=1..3` 控制等级。
- 动画：第三人称复用 `SteveAnimator` 的 riptide 曲线（root pitch 65°、yaw 360°·progress、四肢局部角），旋转期间把三叉戟 prop 画在右手；第一人称新增 `fp::tridentChargePose`（原版 `UseAnim.SPEAR`）和 `fp::riptidePose`（`isAutoSpinAttack` 的手臂姿态）。
- 鞘翅 + 三叉戟：激流不再取消滑翔；滑翔在冲刺的 20 tick 内暂停，结束时把 `elytra_pos_` 同步到新位置并继续滑翔。
- 普通投掷（滑翔时）：滑翔中右键松手改为扔出三叉戟实体（`mc::ThrownTrident`：2.5 b/t、重力 0.05、阻力 0.99），滑翔继续；用现有 `trident.geo.json` 在世界空间绘制（`drawMobModel` + 手动骨骼矩阵），音效 `item.trident.throw` / `item.trident.hit`。地面右键仍是激流。
- 第一人称三叉戟：原版 `trident_in_hand.json` 的 `firstperson_righthand` 是 rotation (0,-90,25)、translation (-3,17,1)/16、scale 1；`ItemTransform.apply` 的顺序是 translate → rotateXYZ → scale → translate(-0.5,-0.5,-0.5)，模型空间 y 向下。我们的网格已居中、视图 y 向上，所以用 `x=+3, y=-17`（`fp::tridentFpDisplay`）。蓄力用 `tridentChargePose`（UseAnim.SPEAR），激流用 `riptidePose`（isAutoSpinAttack），两者都用物品的手臂变换驱动手臂。
- 第三人称三叉戟模型：`trident.geo.json` 换成原版 `TridentModel.createLayer` 的 5 个盒子（pole/base/三个 spike，32x32 贴图），贴图 `entity/trident.png` 从 client.jar 提取到 `mods/mc_adapter/trident.png`（Mojang 资产，不提交），`CreateTridentTexture` 在 slot 30 建表。原版 `TridentSpecialRenderer` 对模型做 `scale(1,-1,-1)`；我们已把 Java y 转成 Bedrock y，所以 trident 骨骼用 `rotation [90,0,0]`（kBasis 是反射，实际 -90°）让叉尖朝前。2D 手持图标对三叉戟关闭，3D 道具只要手持就画，并用手臂完整 delta 跟随动画。
- 未做：投掷的实体命中伤害（目前只做静态世界射线）、拾取/Loyalty、第一人称相机旋转。
- 音效：`tools/extract_mc_sounds.py` 的 `EVENTS` 已加入三个 riptide 事件，需重新提取一次。
- `riptide_require_water` 默认 0（适配器还没有水/雨检测，设为 1 会拒绝释放）。
- 未做：旋转接触实体伤害、水/雨判定、原版实体碰撞。

## 9. 待验证 / 未覆盖

- 原版实机 trace 尚未采集；第 3.4/4.2 的数值是按源码递推模拟得到的，不是游戏内实测。
- 法环引擎的碰撞表示（`CastStaticRay` 的掩码/多高度）需要按实际地形验证；原版 `move` 还会处理 entity collision、sneak edge back-off、stuck speed multiplier，适配器不需要 1:1 复刻。
- 法环引擎的“贴地吸附”可能在低空把玩家拉回地面；滑翔期间必须每 tick 写位置并保持 fall timer 0，需实机确认不会被 `PadManipulator` 强制判着地。
- 鞘翅耐久（每 20 tick 扣 1）和 `ELYTRA_GLIDE` game event 只在生存模式有意义；演示/创造模式可先不接。
- 烟花物品的获取/使用键、珍珠的获取/使用键属于背包/输入层，不在本文逆向范围。
- 原版 `getFlyingSpeed()`（`Player` 冲刺 0.026 / 普通 0.02，创造飞行 0.4）只影响 `travelInAir`，不影响鞘翅分支；如果适配器还要复刻普通空中移动，再单独记录。

## 10. 源码方法索引（便于复查）

| 行为 | 类.方法 |
|---|---|
| 滑翔分支 | `LivingEntity.travel`, `travelFallFlying` |
| 滑翔物理 | `LivingEntity.updateFallFlyingMovement` |
| 撞墙伤害 | `LivingEntity.handleFallFlyingCollisions` |
| 状态/耐久 | `LivingEntity.updateFallFlying`, `canGlide`, `canGlideUsing`, `isFallFlying`, `stopFallFlying` |
| 开始滑翔 | `Player.tryToStartFallFlying`, `startFallFlying`, `canGlide` |
| 重力 | `LivingEntity.getDefaultGravity`, `getEffectiveGravity`, `Entity.getGravity`, `applyGravity` |
| 视线 | `Entity.getLookAngle`, `calculateViewVector` |
| 碰撞 | `Entity.move`, `collide` |
| 旧位置 | `Entity.oldPosition`, `ServerLevel.tickNonPassenger` |
| 烟花 | `FireworkRocketEntity.tick`, `FireworkRocketItem.use`, `Fireworks.flightDuration` |
| 珍珠投掷 | `EnderpearlItem.use`, `Projectile.shootFromRotation`, `Projectile.getMovementToShoot` |
| 珍珠飞行 | `ThrowableProjectile.tick`, `getDefaultGravity` |
| 珍珠命中 | `ThrownEnderpearl.onHit`, `isAllowedToTeleportOwner`, `canTeleport` |
| 珍珠 tick | `ThrownEnderpearl.tick`（死亡消失规则、区块 ticket） |

## 11. 鞘翅模型与 UV（渲染层，2026-11-04 核对）

模型数据来自 `ElytraModel.createLayer()`，渲染挂接来自 `WingsLayer.render()`，姿态来自 `ElytraModel.setupAnim()` + `PlayerElytraAnimationState`。

### 11.1 几何

```text
left_wing : PartPose.offsetAndRotation(5, 0, 0, 15deg, 0, -15deg)
            texOffs(22,0).addBox(-10, 0, 0, 10, 20, 2, CubeDeformation(1))
right_wing: PartPose.offsetAndRotation(-5, 0, 0, 15deg, 0, 15deg)
            texOffs(22,0).mirror().addBox(0, 0, 0, 10, 20, 2, CubeDeformation(1))
```

- Java 模型空间 y 向下、x 向左、z 向后；换到 Bedrock/`mc-core` 的 y 向上：`bedrock_y = 24 - java_y`。因此两翼的 cube origin 都是 `(-5, 4, 2)`、pivot 是 `(±5, 24, 2)`（z=2 见 11.2）。
- `WingsLayer.render()` 在绘制前 `poseStack.translate(0, 0, 0.125)`，即 **+2 px（+Z）**，所以两翼在 Java z 2..4，贴在身体背面之外。geo.json 里的 z=2 就是这个平移，不是笔误。

### 11.2 姿态

- `setupAnim()` **覆盖** `createLayer()` 的 rest rotation：`xRot/zRot/yRot = elytraRotX/Y/Z`，右翼 `yRot`、`zRot` 取反，`xRot` 相同。
- `leftWing.y = isCrouching ? 3 : 0`：**只有蹲下**把翅膀整体下移 3 px；滑翔不下移。所以需要 `elytra_wings` 和 `elytra_wings_crouch` 两个文件，滑翔复用站立文件。
- 角度目标见 `elytra_models.hpp`（站立 15/0/-15，蹲下 40/5/-45，滑翔 x=15+5f、z=-(15+75f)）。

### 11.3 Box UV 布局（关键）

`ModelPart.Cube` 的 unwrap 是固定的（反编译 1.21.8 逐行核对），**与 mirror 无关**：

```text
[u .. u+d]                 West  (-X)
[u+d .. u+d+w]             North (-Z, front)
[u+d+w .. u+2d+w]          East  (+X)
[u+2d+w .. u+2d+2w]        South (+Z, back)
[u+d .. u+d+w]   at V-top  Top   (+Y)
[u+d+w .. u+2d+w] at V-top Bottom(-Y)
```

- `mirror` 不会交换 East/West 的矩形；它是在建顶点前交换 min/max X 的角色，于是几何镜像、同一顶点索引拿到同一 UV，贴图在每个面上自然反向。
- 之前 `src/entity_model.cpp` 把 East/West 矩形写反、并把 mirror 当成“交换矩形 + 全面翻转”，导致鞘翅外侧边缘采样到几乎全透明的贴图，轮廓被切错；已按上表修正。`src/rig.cpp` 的 `boxFaces()` 一直是正确版本，可作为对照。
- 验证方法：离线按 Java `ModelPart.Cube` + `PartPose` 渲染，和 `entity_model.cpp` 的端口逐像素比对；站立/滑翔均为 0 差异。

### 11.4 源码方法索引（补充）

| 行为 | 类.方法 |
|---|---|
| 翼几何 | `ElytraModel.createLayer` |
| 翼姿态 | `ElytraModel.setupAnim`, `ElytraAnimationState.tick` |
| 渲染挂接 / +2px | `WingsLayer.render` |
| Box UV / mirror | `ModelPart.Cube.<init>`, `ModelPart.Polygon.<init>` |
