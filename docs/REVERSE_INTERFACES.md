# 需要逆向的宿主接口清单

Minecraft 动作（攻击、盾牌、弓箭、受伤、第一人称……）的规则已经在核心里（`mc::Session`）。接一个新游戏时真正的工作量是：**把下面这些宿主数据和动作找出来**。每一项对应契约 `include/mc/contracts/host_gameplay.hpp` 里的一个 `HostFeature`，适配器只实现自己找到并验证过的，其余保持默认（无操作），`Session::missingHostFeatures()` 会告诉你还缺什么。

总流程见 [PORTING_PLAYBOOK.md](PORTING_PLAYBOOK.md)，工具在 `tools/reverse/`。写规则：**先只读，有证据（RTTI、setter 反汇编、当前值）才写**，一次只写一个字段并记住原值、能还原。

## 0. 总览

| 接口（`HostFeature`） | 解锁的 MC 行为 | 逆向难度 | 需要写宿主吗 | 没有它时 |
|---|---|---|---|---|
| `InputSuppression` | 攻击/使用时原生角色不再挥刀、格挡 | 低（输入层，不碰游戏内存） | 否 | 鼠标点击同时触发原生动作和 MC 动作 |
| `PlayerVitals` | 红心 = 真实血量；受伤、回血可见 | 低-中（只读） | 否 | 红心是预设值，不随受伤变化 |
| `EntityEnumeration` | 近战/弓箭能命中敌人（把敌人注册成 `EntityId`） | 中（找实体列表） | 否 | 打不到任何东西 |
| `IncomingDamageEvents` | 盾牌格挡、不死图腾、受击反馈 | 高（要 hook 伤害函数，或轮询血量差分） | 是（补血或改伤害） | 没有盾牌/图腾 |
| `FirstPersonCamera` | 第一人称：手臂、持物、视角在眼睛处 | 中（相机对象可写性） | 视方案 | 只有第三人称 |
| `GroundedFlag` | 鞘翅/摔落伤害判定准确 | 低-中（只读） | 否 | 用"垂直速度小"估计，滑翔中会误判 |
| `WorldRaycast`（`IPhysicsAdapter::raycastWorld`，非 `HostFeature`） | 选方块、放置、射箭：射线要打到游戏的真实地形和敌人 | 高（找游戏自己的射线查询并能安全调用） | 否（只调用） | 没有它方块放不下去、箭射不出去 |
| `BlockCollision`（`createBlockCollider`，非 `HostFeature`） | 放下的方块能挡住玩家和敌人 | 高（注册进游戏物理）或 0（在我们自己的层拦截移动） | 是 | 方块只是看得见、穿得过 |

已经完成、可作为参照的宿主数据（契约里不是 `HostFeature`，但同样需要逆向）：玩家位置/速度、相机矩阵与 FOV（`LiveSample`）、真实朝向（`IInputAdapter::getPlayerFacingYaw`）、游戏深度缓冲（遮挡，手册 §6.5）、隐藏原生模型（`ModelHider`）。

**推荐顺序**：`InputSuppression` → `PlayerVitals` → `EntityEnumeration` → 敌人受击（`ICombatAdapter::processHit`）→ `IncomingDamageEvents` → `FirstPersonCamera` → `GroundedFlag`。前两项不需要写游戏内存，先拿到收益。

---

## 1. InputSuppression —— 吞掉原生战斗输入

- **契约**：`IHostGameplay::setNativeCombatInputSuppressed(bool)`；`Session::setActive` 自动开/关，析构时释放。
- **做什么**：Steve 模式下，鼠标按键不能让原生角色攻击/格挡/使用道具；移动、镜头、菜单照常。
- **怎么找**：先弄清游戏从哪读鼠标。
  1. 对 `dinput8.dll`/`user32.dll` 的候选入口下只读的计数钩子（`GetAsyncKeyState`、`GetRawInputData`、`IDirectInputDevice8::GetDeviceState/GetDeviceData`），点击时看哪个有调用。
  2. 找到后，在**同一位置**把鼠标按键位清零（只清左/右键，不动位移和滚轮）。
  3. dinput8 代理 DLL 只转发 `DirectInput8Create` 时，需要包装返回的 `IDirectInput8` → `CreateDevice` → 设备的 `GetDeviceState`。
- **验证**：F6 开启后点左键，原生角色不动；F6 关闭后恢复；窗口失焦/切菜单不会卡住按键。
- **风险**：低。不要吞键盘移动键。
- **只狼状态**：未做。游戏用哪条输入路径未确认；当前 loader **只有 `WndProc` 钩子，没有 DirectInput 设备包装**（`GetDeviceState` 代理还不存在）。线索：`PlayerIns+0x58` 是 `PadManipulator`（`0x29D4B30`），`PlayerIns+0x50` 是 `PlayerCtrl`（`0x29D5508`），它们是原生输入落地的位置，可作为读侧验证点（点击时哪些字段变化）。
- **法环状态**：未做。导入表有 `DINPUT8.dll`，但必须先挂只读计数钩子确认鼠标走 `GetDeviceState` 还是 RawInput。详见 [ELDENRING_REVERSE.md](ELDENRING_REVERSE.md) §5。
- **UE 参照**（`~/wukong-steve`）：把 `Evt_InputCastSkill` 的回调换成自己的函数；我们没有这样的事件表，需要走输入层。

## 2. PlayerVitals —— 读玩家真实血量

- **契约**：`IHostGameplay::getPlayerVitals(HostVitals&)`；`Session` 每帧把 `health/max_health` 换算成 20 个半心写进 HUD，值不可信（≤0 的最大值、NaN）时保持原值。
- **怎么找**（差分，全程只读）：
  1. 玩家指针树快照（`ptrtree.py snap`，深度 4）。
  2. 让角色**掉血**（摔落、被打）再抓一次，满血时再抓一次。
  3. 找整数/浮点对 `(当前, 最大)`：当前随受伤下降、回血上升，最大值不变；两者在多个副本里同时出现更可信。
  4. 用 RTTI（`rtti_survey.py`）确认所在对象类名（通常是"角色数据/属性模块"），记下 `根 → 模块 → 字段` 的偏移链。
- **验证**：吃药/受伤时读数与屏幕血条同步变化；死亡、读档、过场时读数无效而不是 0。
- **风险**：只读，低。
- **只狼状态**：**已实现并在真实游戏里验证**。链：`[[player+0x10b8]+0x1e8]` 是 `SprjChrDataModule`（虚表 RVA `0x2A8BE18`，读之前用它做类名校验），`int32` 当前血量 `+0x130`、最大血量 `+0x138`（受击 1120→934→748→…→128、吃药回到 1024，最大值恒为 1120）。另有 `+0x134`（显示用平滑血量）、`+0x148`（架势/躯干，基准 420）、`+0x170`（最近一次所受伤害量，可做免 hook 的受击差分）、`+0x174`（最近一次躯干伤害），后两项是 §5 的线索。代码：`sekiro_live` 的 `readVitals`、`SekiroAdapter::getPlayerVitals`，`supportedFeatures()` 已声明 `PlayerVitals`。
- **法环状态**：**读取已实现并有单元测试**（`eldenring_live.hpp`），血量链实机验证：`ChrIns+0x190` → 槽 0 `CSChrDataModule`（虚表 `0x2A380B8`，`+0x8` 属主），`int32` 当前 `+0x138`、有效最大 `+0x13C`、基础 `+0x144`。玩家指针 `+0x1E508` 未在真机读过，尚未声明 `PlayerVitals`。见 [ELDENRING_REVERSE.md](ELDENRING_REVERSE.md) §1。
- **UE 参照**：直接读玩家的属性组件。

## 3. EntityEnumeration —— 枚举敌人

- **契约**：`IHostGameplay::enumerateEntities(HostEntityInfo*, size_t)`，返回稳定的 `EntityId`（≥2，不用指针/句柄当 id）、位置（规范空间，厘米）、血量、是否敌对。适配器内部据此维护已注册实体表（`registerEntity/unregisterEntity`），`raycastWorld` 命中时返回对应 `hit_entity`。
- **怎么找**：
  1. 从已知的世界管理器（只狼：`WorldChrMan`）出发，用指针树差分找"实体数组/链表"：敌人出现/消失时数组长度和元素变化。
  2. 元素通常是同一基类（`ChrIns`）的指针：用 RTTI 确认类名；位置字段偏移应与玩家的 `+0x1050` 同构（同一类，同一偏移）。
  3. 区分敌我：队伍号/阵营字段（差分：自己和敌人不同）。
  4. 一个死亡/卸载的实体必须从表里消失；读到空指针或非法位置不能崩。
- **验证**：站在已知敌人旁，枚举出的位置与屏幕上敌人的位置一致（用相机矩阵投影到屏幕上对照截图）。
- **风险**：只读，低；枚举要容错，列表在加载时会被改写。
- **只狼状态**：**读取已实现，待实机验证命中**。`WorldChrMan+0xC8` → `WorldBlockChr`（虚表 RVA `0x2A2EB10`）；`+0x80` 槽位数（实测 144），`+0x88` 槽位数组，步长 `0x38`，槽内 `+0` 是 `EnemyIns*`（虚表 `0x2A27F28`，读前必须校验）。字段：`+0x68` 角色 id，`+0x70` 阵营（5 敌对、9 中立、0/1 己方）。**位置用 `+0x1050`（与玩家同构，配对副本 `+0x1060`）；`+0xE0` 只是静态出生坐标**，绝大多数未加载实体 `+0x1050` 全零，用它判活跃。`EntityId` 由 `EnemyTracker` 分配的稳定小整数（指针只是帧内身份，槽位会被回收，id 永不复用）。命中判定：对每个已注册敌人做 45 cm 半径、190 cm 高的竖直胶囊求交（游戏自己的射线未接入，所以**不会被墙挡住**，受 4.5 m 触及距离限制）。
- **法环状态**：数组结构已知（`WorldChrMan+0x1E270`，`+0x08` 容量，`+0x10` 起 `EnemyIns*`，虚表 `0x2A47090`）。注意 `+0x1E5E0` 不是敌人容器（是网络同步表）。**位置原点会按网格跳变，玩家与敌人是否共用同一原点未证明，暂不实现。** 见 [ELDENRING_REVERSE.md](ELDENRING_REVERSE.md) §1、§2、§4。
- **UE 参照**：`GetAllActorsOfClass`，引擎自带。

## 4. 敌人受击（`ICombatAdapter::processHit`）

- **契约**：已存在。`processHit` 只改状态；击退由核心通过 `triggerStaggerOrRagdoll` 触发一次。
- **做什么**：把 `HitIntent.damage` 应用到敌人。
- **怎么做**（由低风险到高风险，逐级升级，每一级先在单一敌人上验证）：
  1. **写敌人当前血量**：读出 → 减 → 写回；只写一个字段，记住原值。伤害按敌方最大血量比例换算（核心给的是 MC 伤害，适配器换算成该敌人的比例，参考 wukong-steve 的"按最大生命比例输出"）。
  2. 血量写到 ≤0 时敌人是否真的死亡（有的游戏需要走死亡流程而不是直接改数）。
  3. 需要受击动画/硬直时，才去找游戏的"受击"函数并调用它（要 hook/调用游戏函数，风险高）。
- **验证**：打一个杂兵，血条下降；血量归零后走游戏自己的死亡动画；不会出现"打不死/卡无敌"。
- **只狼状态**：**已实现，待实机验证**。敌人数据模块在 `[[enemy+0x10b8]+0x1f8]`（玩家的是 `+0x1e8`），虚表 `0x2A8BE18`；`+0x130` 当前血量，**敌人的最大血量在 `+0x160`**（玩家在 `+0x138`），`+0x148` 架势。`HostHealthWriter` 只写 `+0x130` 一个 int32：写前校验类名与数值（`0≤hp≤max`），写后读回确认；**对敌人只降不升，对玩家只升不降**。核心的伤害是最大血量的 5 %（钻石剑，`HitIntent.max_hp_percent`）。
- **法环状态**：敌人侧未分析。玩家侧已读过 `PlayerDamageModule::vfunc[13]`（`0x14044CCA0`）的控制流，还没抓到真实命中。见 [ELDENRING_REVERSE.md](ELDENRING_REVERSE.md) §3。

### 3.1 敌人数据模块怎么找（只狼，2026-10-07 实机，89 个敌人）

不要写死偏移。血量模块（`SprjChrDataModule`，虚表 RVA `0x2A8BE18`）的 **`+0x8` 是它的主人角色指针**，用它做校验：在角色对象里找一个指针，指向"虚表是数据模块、且 `+0x8` 等于这个角色"的对象（`findOwnedDataModule`）。多数敌人类型直接从 `EnemyIns+0x2288`（或 `+0x2188..+0x2268` 附近）指向它；玩家和少数敌人走 `+0x10b8` 容器；个别类型走 `0x1ff8→+0x18`、`0x1150→+0x128`、`0x1b58→+0xe8`。血量 `+0x130`，最大血量 `+0x160`。**写血能让敌人掉血/死亡，但不会让它硬直或击退**：受击反应需要逆向游戏自己的受击流程（容器里的 `SprjEnemyDamageModule`、`SprjEnemyKnockBackModule`、`SprjChrActionRequestModule`），尚未做。

## 5. IncomingDamageEvents —— 玩家将受的伤害

- **契约**：`IHostGameplay::drainIncomingDamage(IncomingDamage*, size_t)` + `refundPlayerDamage(float)`。
- **做什么**：盾牌（正面格挡时把伤害补回）、不死图腾（致死伤害时补血并给效果）、受击反馈。
- **怎么做**：
  1. **不 hook 的方案**（先试）：每帧读玩家血量（§2），血量下降即视为受伤，`amount = 上一帧 - 本帧`，`health_before = 上一帧`；格挡时写回补血。缺点：伤害已经生效（会有受击动画），且无法知道攻击者方向（可用最近敌人近似）。
  2. **hook 方案**：找到游戏的"应用伤害"函数（反汇编：入参含目标、数值；它会写 §2 的血量字段，在写点下硬件断点/用 Cheat Engine 式访问追踪定位），inline hook，读取伤害并按规则修改后再调用原函数。风险高，崩过的盲写教训见手册。
- **验证**：被打一次只产生一个事件；格挡时血量不降；图腾触发后血量恢复到半心。
- **只狼状态**：未做，依赖 §2。线索（已验证虚表）：`[[player+0x10b8]+0x268]` 是 `SprjPlayerDamageModule`（`0x2A7DA48`）；要 hook 的应用伤害函数还没定位。
- **法环状态**：未做。线索：`0x14044CCA0` 入口可读 `[rdx+0x48]`（伤害）与 `[rdx+0x1A0]`（命中点），待抓真实命中并确认线程。见 [ELDENRING_REVERSE.md](ELDENRING_REVERSE.md) §3、§5。
- **UE 参照**：把原生伤害事件 `Evt_TriggerNormalDamageEffect` 的回调换成自己的函数，盾牌在这里按正面角度拦截。

## 6. FirstPersonCamera —— 第一人称

- **契约**：`HostFeature::FirstPersonCamera`（接口待第一个实现该特性的游戏定型：需要"把相机放到眼睛处并恢复"和"隐藏自己的头"）。
- **怎么做**：两条路：
  1. **自绘**（只狼已有的路径）：游戏相机不动，我们的渲染器在屏幕上画第一人称手臂和持物（视图矩阵用固定的"手臂相机"）。不需要写游戏内存。
  2. **移动游戏相机**：找相机对象的"目标位置/距离"字段（差分：切换镜头模式时变化）；写入要可还原，且每帧覆盖（游戏每帧会重写）。
- **验证**：F5 式切换前后相机回到原位；穿墙/碰撞时不抖。
- **只狼状态**：未做；建议走自绘路径。（写相机后再 Present 对已经渲染完的那一帧无效，要改相机必须在游戏渲染**之前**改并还原；眼高在只狼的 Y 上米制空间里是 `+1.71 m` 的 Y，不是 Z。）
- **法环状态**：未开始。游戏是 D3D12，现有 D3D11 渲染钩子不能复用。见 [ELDENRING_REVERSE.md](ELDENRING_REVERSE.md) §5。

## 7. GroundedFlag —— 真实着地标志

- **契约**：`InputSnapshot::on_ground` + `on_ground_is_estimate=false`。
- **怎么找**：玩家指针树差分：站地/跳起/落地三种状态各抓一次，找 0/1（或枚举）字段，与"垂直速度"对照。
- **只狼状态**：**已实现**。`[[player+0x10b8]+0x240]` 是 `SprjPlayerFallModule`（`0x2A821F0`），`int32` `+0x40` 为 -1 表示着地、≥0 表示空中；未知/类名不符时按"着地"处理（不会误触发滑翔）。读不到时退回垂直速度估计（`on_ground_is_estimate`）。
- **法环状态**：未做（`CSChrFallModule` 虚表 `0x2A3A8B0` 只有类名，没有字段）。

---

## 7a. WorldRaycast —— 游戏的世界射线查询

- **契约**：已存在，`IPhysicsAdapter::raycastWorld(start, end, ignore)`，返回命中点、法线、命中的是方块还是实体。
- **做什么**：选方块（瞄准）、放置（取命中面的法线）、射箭（箭是否撞墙或命中敌人）、TNT 的遮挡判断都依赖它。
- **怎么找**：找游戏自己的射线/碰撞查询函数（Havok 一类）：入参起点、终点、过滤；出参命中点、法线、命中对象。只读调用，先在空场景里朝地面、墙各打几次，对照屏幕；还要确认它必须在哪个线程调用。
- **验证**：从相机朝地面射，命中点落在屏幕准星指向的地面；朝墙射，法线朝外；朝敌人射，能返回该敌人的身份。
- **风险**：中。调用游戏函数要保证线程和参数合法，崩过的盲调教训见手册。
- **只狼状态**：**未接到游戏**。`DantelionEngineContext::RaycastWorld` 在没设置 `CustomRaycast` 时直接返回 false；`SekiroAdapter::raycastWorld` 目前只在影子碰撞盒和已注册实体里匹配，游戏的真实地形不参与。敌人命中用的是自己的竖直胶囊求交（见 §3），**不会被墙挡住**。
- **法环状态**：未开始。

## 7b. BlockCollision —— 方块的碰撞

- **契约**：已存在，`IPhysicsAdapter::createBlockCollider / destroyBlockCollider`。
- **做什么**：我们放下的方块要能挡住玩家和敌人。
- **两条路**：
  1. 把方块注册进游戏的物理空间（要逆向物理世界的创建/加入接口，风险高）。
  2. 不碰游戏物理，在我们自己的层里拦截：每帧检查玩家（和敌人）是否进入了方块，进入则把位置推回去。不需要逆向，但手感较差，且写位置有崩溃风险（见只狼"盲写崩溃"的教训）。
- **只狼状态**：**只有影子碰撞盒**（`HavokStaticBoxCollider` 记录在 `colliders_` 里），没有注册进游戏。
- **法环状态**：未开始。

---

## 8. 每项逆向的交付物（放进新游戏适配器目录）

1. **偏移/签名常量**（带版本号和验证日期）放进 `*_live.hpp` 的 `layout` 命名空间，不要散落在 .cpp 里。
2. **平台无关的读取逻辑 + 单元测试**（用假内存），含"拒绝坏数据"的反例：空指针、NaN、未加载世界、结构校验失败。
3. **只读验证记录**：什么操作下读数怎么变（写进手册案例章节）。
4. `supportedFeatures()` 里加上对应位——**只有在真实游戏里验证过才加**。
5. 若写了宿主内存：恢复路径（`restore`）、写前的类名/值校验、失败时报告而不是假装成功。
