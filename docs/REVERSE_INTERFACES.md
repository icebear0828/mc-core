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
- **只狼状态**：未做。游戏用哪条输入路径未确认。
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
- **只狼状态**：未做。预期在 `ChrIns` 的某个模块里；需要两次差分快照确认。
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
- **只狼状态**：未做。
- **UE 参照**：`GetAllActorsOfClass`，引擎自带。

## 4. 敌人受击（`ICombatAdapter::processHit`）

- **契约**：已存在。`processHit` 只改状态；击退由核心通过 `triggerStaggerOrRagdoll` 触发一次。
- **做什么**：把 `HitIntent.damage` 应用到敌人。
- **怎么做**（由低风险到高风险，逐级升级，每一级先在单一敌人上验证）：
  1. **写敌人当前血量**：读出 → 减 → 写回；只写一个字段，记住原值。伤害按敌方最大血量比例换算（核心给的是 MC 伤害，适配器换算成该敌人的比例，参考 wukong-steve 的"按最大生命比例输出"）。
  2. 血量写到 ≤0 时敌人是否真的死亡（有的游戏需要走死亡流程而不是直接改数）。
  3. 需要受击动画/硬直时，才去找游戏的"受击"函数并调用它（要 hook/调用游戏函数，风险高）。
- **验证**：打一个杂兵，血条下降；血量归零后走游戏自己的死亡动画；不会出现"打不死/卡无敌"。
- **只狼状态**：未做，依赖 §3。

## 5. IncomingDamageEvents —— 玩家将受的伤害

- **契约**：`IHostGameplay::drainIncomingDamage(IncomingDamage*, size_t)` + `refundPlayerDamage(float)`。
- **做什么**：盾牌（正面格挡时把伤害补回）、不死图腾（致死伤害时补血并给效果）、受击反馈。
- **怎么做**：
  1. **不 hook 的方案**（先试）：每帧读玩家血量（§2），血量下降即视为受伤，`amount = 上一帧 - 本帧`，`health_before = 上一帧`；格挡时写回补血。缺点：伤害已经生效（会有受击动画），且无法知道攻击者方向（可用最近敌人近似）。
  2. **hook 方案**：找到游戏的"应用伤害"函数（反汇编：入参含目标、数值；它会写 §2 的血量字段，在写点下硬件断点/用 Cheat Engine 式访问追踪定位），inline hook，读取伤害并按规则修改后再调用原函数。风险高，崩过的盲写教训见手册。
- **验证**：被打一次只产生一个事件；格挡时血量不降；图腾触发后血量恢复到半心。
- **只狼状态**：未做，依赖 §2。
- **UE 参照**：把原生伤害事件 `Evt_TriggerNormalDamageEffect` 的回调换成自己的函数，盾牌在这里按正面角度拦截。

## 6. FirstPersonCamera —— 第一人称

- **契约**：`HostFeature::FirstPersonCamera`（接口待第一个实现该特性的游戏定型：需要"把相机放到眼睛处并恢复"和"隐藏自己的头"）。
- **怎么做**：两条路：
  1. **自绘**（只狼已有的路径）：游戏相机不动，我们的渲染器在屏幕上画第一人称手臂和持物（视图矩阵用固定的"手臂相机"）。不需要写游戏内存。
  2. **移动游戏相机**：找相机对象的"目标位置/距离"字段（差分：切换镜头模式时变化）；写入要可还原，且每帧覆盖（游戏每帧会重写）。
- **验证**：F5 式切换前后相机回到原位；穿墙/碰撞时不抖。
- **只狼状态**：未做；建议走自绘路径。

## 7. GroundedFlag —— 真实着地标志

- **契约**：`InputSnapshot::on_ground` + `on_ground_is_estimate=false`。
- **怎么找**：玩家指针树差分：站地/跳起/落地三种状态各抓一次，找 0/1（或枚举）字段，与"垂直速度"对照。
- **只狼状态**：未做，目前用 `|vy|<2 m/s` 估计（`isPlayerOnGround`）。

---

## 8. 每项逆向的交付物（放进新游戏适配器目录）

1. **偏移/签名常量**（带版本号和验证日期）放进 `*_live.hpp` 的 `layout` 命名空间，不要散落在 .cpp 里。
2. **平台无关的读取逻辑 + 单元测试**（用假内存），含"拒绝坏数据"的反例：空指针、NaN、未加载世界、结构校验失败。
3. **只读验证记录**：什么操作下读数怎么变（写进手册案例章节）。
4. `supportedFeatures()` 里加上对应位——**只有在真实游戏里验证过才加**。
5. 若写了宿主内存：恢复路径（`restore`）、写前的类名/值校验、失败时报告而不是假装成功。
