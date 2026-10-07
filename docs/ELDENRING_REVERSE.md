# 艾尔登法环（eldenring.exe）逆向记录

对应 `docs/REVERSE_INTERFACES.md` 的每个接口，这里只记**证据**和**还没证实的部分**。写法规则同只狼：先只读，有证据才写；每条标明证据等级。

- 环境：Steam 版，**离线、不经 EAC** 启动（直接运行 `eldenring.exe` 并放 `steam_appid.txt`）。联网 + EAC 下做任何注入或内存读写都有封号风险。
- 所有 RVA 都是**这一个版本**的固定值。上线前必须换成签名/RTTI 查找（只狼的教训：游戏更新就失效）。
- 代码：`adapters/eldenring/include/eldenring_live.hpp`（目前只有 `readVitals` / `readPlayerVitals`）。

证据等级：

| 等级 | 含义 |
|---|---|
| **A 实机** | 在运行中的游戏里读到、并和游戏内操作对照过（逆向方提供的日志） |
| **B 反汇编** | 指令字节和跳转目标已逐条核对自洽；语义仍可能是推断 |
| **C 推断** | 没有直接证据，仅合理猜测，**不得写进代码** |

---

## 1. 已确认

### 玩家血量 / 精力（`PlayerVitals`，已实现）
链：`PlayerIns` →（`+0x190`）模块容器 → 槽 0 = `CSChrDataModule`。

| 项 | 值 | 等级 |
|---|---|---|
| `CSChrDataModule` 虚表 RVA | `0x2A380B8` | A（RTTI） |
| 模块属主 | `[module+0x8]` = 所属 `ChrIns`。`0x14043D250` 就是 `mov rax,[rcx+8]; ret`（`getOwnerChr`），对 `PlayerDamageModule` 同样成立，所以是模块基类的字段 | B |
| 当前 HP | `+0x138`（int32）。受击/回血全程只有它变：553→487→444→401→335→292→249→183→140，+250 回 390，−82、−98，再 +250 | A |
| 有效最大 HP | `+0x13C`（穿红琥珀链坠 522→553）。`+0x140` 同步变化（也是 553） | A |
| 基础最大 HP | `+0x144`（始终 522） | A |
| 精力 | `+0x154` 当前（翻滚/奔跑下降、线性回升），`+0x158` 上限，`+0x15C` 基础 | A |
| 专注（FP） | `+0x148/+0x14C/+0x150`，**只确认了位置，没扣过 FP，三个字段的角色未区分** | C |

### 模块容器
- `ChrIns+0x190` 指向 64 个模块指针的表（`registerModule` 在 `0x1404C5940` 检查 `index < 0x40`）。B
- 槽：`0` 数据、`1` 动作标志（`ActionFlagModule`）、`0x0D` 物理、`0x13` 受击（玩家 `CSPlayerDamageModule` 虚表 `0x2A3A3E0`，敌人 `0x2A3A0D8`）。槽号来自 `registerModule` 的调用点未逐个核对，**除 0 和 1、0x0D 外属 C**。

### 位置 / 速度 / 朝向（读取未实现）
`PhysicsModule`（槽 `0x0D`，虚表 `0x2A3C890`）：

| 项 | 值 | 等级 |
|---|---|---|
| 位置 | `+0x70`（float X,Y,Z），走动时连续变化；与 `ChrIns+0x80` 逐位相同（同一实体的两份拷贝） | A |
| 线速度 | `+0x120..+0x128` | A |
| 朝向 | `+0x50..+0x5C` 四元数；`+0x54`、`+0x5C` 的平方和为 1（纯绕 Y 轴），**还没验证它和跑动方向一致** | A（数值）/ C（含义） |

### 敌人列表
| 项 | 值 | 等级 |
|---|---|---|
| 容器 | `WorldChrMan+0x1E270`：`+0x08` uint32 容量（实测 115），`+0x10` 起为 `EnemyIns*` 数组，步长 8。**注意：按 `fromsoftware-rs` 的排布推算，它只是 `chr_sets[196]`（起点 `+0x1DED8`）中下标 115 的那一个 `ChrSet`，只读它会漏敌人，见 §7.2** | A（读数）/ C（是否完整） |
| 元素 | 虚表 RVA `0x2A47090`（`EnemyIns`），非空槽 51 个全部通过，且 `modules[0]+0x8 == chr` 全部通过 | A |
| 角色字段 | `+0x64` NPC id（样本：100、110、2271、4300、6060）| A |
| 角色字段（存疑） | `+0x68`：玩家 0、一个野外敌人 5（仅这两个样本）。**外部参考里 `+0x68` 是 `chr_type`，阵营 `team_type` 在 `+0x6C`**，所以这个值更可能是角色类型，不是阵营，见 §7。`+0x60` 逆向方称实例句柄，外部参考里是 `npc_param_id`，句柄在 `+0x08` | C |
| 全局指针 | `WorldChrMan` 全局 `base+0x3D69FF8`（固定 RVA）；玩家 `[WorldChrMan+0x1E508]`（**没有在真机上读过**） | B / C |

敌人的数据模块仍沿用"槽 0 + 虚表 + owner"校验；敌人最大血量字段、死亡和卸载的识别未验证。

---

## 2. 已否定 / 已撤回（不要再用）

| 旧说法 | 事实 |
|---|---|
| `WorldChrMan+0x1E5E0` 是敌人容器（`WorldBlockChr`） | 它是**按句柄查表的网络同步表**：构造时分配 `0x14A0` 字节，容量 `[+8] = 116`（= 配置值 111 + 5），下标 `(handle>>11)&0xFF`。116 个槽里只有 3 个非空，类型是 `NetChrSetSync` / `NetOpenFieldChrSetSync`，**一个 `EnemyIns` 都没有** |
| `team_type` 在 `ChrIns+0x70`、值 5/9 | `+0x70` 是从只狼抄来的。曾改为 `+0x68`（玩家 0、敌人 5），但外部参考指出 `+0x68` 是 `chr_type`、`team_type` 在 `+0x6C`（u8）。**待读 `+0x6C` 后定** |
| 最大 HP 在 `+0x140`、基础在 `+0x13C` | 反了：`+0x13C` 有效最大、`+0x144` 基础 |
| `ReceiveHit` 的 AOB `48 89 5C 24 10 56 48 83 EC 30 48 8B 4A 08` | 全镜像 993 处命中，只是通用函数序言 |
| 受击入口 `RCX = DamageModule, RDX = 攻击者, R8 = HitParamData, R9D = uint32` | `0x14044EC00` 开头就用 `[rdx+8]` 覆盖了 RCX；`RDX` 是一对 `(句柄, 指针)`，`R8` 是**带虚表的对象**（至少 14 个虚函数）；`R9D` 是 `vfunc[6]` 这个 thunk 从栈上取的一个 byte |
| `HitEnvelope.request_token`、"全局打击 ID" | 没有证据。`+0x38` 的 64 位值受击瞬间从 `0x0000000100000000` 变为 `0x00000001B71C0000` 然后恢复，变化的是**低 32 位** |
| `DamageModule+0x80` 是击退冲量 | `W = 1.0`、`Y` 分量逐位相同，是**位置点**（世界坐标），不是冲量 |
| `CSChrFallModule` / 其他"已确证"的类名猜测 | 名字来自 RTTI 的才算数；功能性命名（"判定与抗性计算"、"无敌帧检查"）全部撤回 |

---

## 3. 受击管线（只读，未调用）

### 3.1 `PlayerDamageModule::vfunc[13]`（`0x14044CCA0`，B）
`RCX` = 伤害模块，`RDX` = 上下文结构（普通结构，不是多态对象）。被读取的字段：`+0x3C`(byte)、`+0x48`(int)、`+0x54`(int)、`+0x58`(float)、`+0xD8`(byte)、`+0xDA`(byte)、`+0x1A0..+0x1B0`（float4）。

控制流（已核对）：
1. `+0x54 == 2` 且 `0x1404468C0` 返回非 0：**直接命中分支**。读属主的 `ActionFlagModule`（槽 1），`or [flags+0x214],1`，把 `0x3EE` 或 `0x3EF` 写进 `+0x34`（由 `+0x3C`、`+0xDA` 选）；若 `[rdx+0xD8] == 0`，调用属主 `ChrIns` 的虚函数 `[+0x280]`（参数 `[rdx+0x48]`、1）；再调 `0x14044A470`、`0x140446B20`（用 `+0x58`、`+0x1A0`）和 `0x140E3EF30`；返回值为 1。
2. 否则落到 `0x14044CD73`：模块开关 `[module+0xB1]` 非 0 且 `+0x54` 为 2 或 3 时，只把命中点等写进反馈记录（`0x140BB2860`），**不扣血**，返回 0。
3. `0x1404468C0` 在 `ActionFlag+0x40 & 0x6000` 非 0 时提前返回（命中被某种状态避开）。

观测：该函数在线程 2428 上每帧被调用，空调用时 `+0x54 = 0`、`+0x48 = 0xFFFFFFFF`、`RDX` 是固定栈地址。**还没有抓到过一次真实命中的上下文**，所以"`+0x54 = 2` 时扣血"来自代码，不是观测。

### 3.2 `0x14044EC00`（B）
受击事件分发器：`RDX` 的 `(句柄, 指针)` 交给 `0x14044DDD0` 解析出目标，随后依次调用目标虚函数 `[+8]` 与 `R8` 对象的虚函数 `[+0x68]`，最后清零 `[rdx+8]`。虚表 `CSEnemyDamageModule` 的 `vfunc[6]` 是个 thunk（`44 0F B6 4C 24 20 E9 ...`，全镜像唯一）直接跳到这里。

### 3.3 未解决
- 命名（`hit_flag`、`guard_flag`、`stagger_flag` 等）全是猜测：`+0xD8 != 0` 时是**跳过**扣血，不是"免除硬直"。
- 敌人侧的伤害模块有没有对应函数、上下文格式是否一致（我们要打的是敌人）。
- 可能的免 hook 读数：玩家 `DamageModule` 上 `+0x80`（位置点）、`+0xD8 = 2`、`+0x38`；是否模块里内嵌了一份上下文，要用一次真实命中的 `RDX` dump 对照。
- "`vfunc[7]` (`0x14044D050`) 是总派发入口、`0x140652140` 是 `TakeDamage`"目前只有断言，没有反汇编。

---

## 4. 坐标系（部分结论，待验证）

观测：
- 玩家位置读数始终在原点附近几米（`(-1.27,6.0,5.6)`、`(-6.39,6.63,7.2)`、`(-3.5,2.6,-0.9)`、`(-28.7,6.5,26.2)`），跑 300 米后仍然如此。
- 一个被认为静止的敌人两次读数相差**精确整数** `(+56, 0, +40)`，同时玩家读数差 `(+28.0, −1.65, −51.75)`。

结论（仅此，不多）：
- 位置**不是**固定原点的世界坐标，原点会以网格步长跳变。
- 跳变时所有基于"上一帧位置"的计算（速度差分、相机携带、`EnemyTracker`）会出现尖峰。

尚未证明：
- 玩家与敌人是否**共用同一个**原点（共用则 `E − P` 永远正确，不需要换算）。
- 那个敌人是否同一个指针、是否真的静止。
- 95.9 米是否等于玩家真实跑过的距离。
- 命中点 `+0x1A0` 的参考系。

实现约束（在这些验证完之前）：位置只用于同一帧内的相对量；速度用 `PhysicsModule+0x120`，不用位置差分；单帧位移远大于 `速度×dt` 时重置跟踪器；不做块原点换算，也不去找 `Δtile`。

---

## 5. 各接口当前状态

| 接口 | 状态 |
|---|---|
| `PlayerVitals` | **已实现**（`readVitals`），血量链 A。待真机跑 `readPlayerVitals`（玩家指针 `+0x1E508` 未读过）。未在 `supportedFeatures` 声明（还没有适配器类） |
| `EntityEnumeration` | 数组结构已知；**坐标参考系未定，暂不写代码** |
| `GroundedFlag` | 未做。`CSChrFallModule`（`0x2A3A8B0`）只有类名，没有字段 |
| `WorldRaycast`（选方块、放置、射箭） | **未开始，P0**。需要找到游戏自己的射线查询函数（入参、命中点、法线、命中对象、线程）。没有它方块放不下去、箭射不出去 |
| `BlockCollision`（方块挡人） | 未开始。方案二选一：注册进游戏物理，或在我们自己的层拦截移动 |
| `InputSuppression` | 未做。导入表有 `DINPUT8.dll`，但**必须先挂只读计数钩子**确认游戏用 `GetDeviceState` 还是 RawInput |
| `IncomingDamageEvents` | 线索：`0x14044CCA0` 入口可读 `[rdx+0x48]`（伤害）、`[rdx+0x1A0]`（命中点），需要时可改写。待抓真实命中 |
| 敌人受击 | 敌人侧函数未分析 |
| `FirstPersonCamera` / 渲染 | **未开始**。导入表只有 `d3d12.dll`/`dxgi.dll`（D3D12），现有 D3D11 渲染钩子全部不能复用：需要命令队列（`ExecuteCommandLists`）、深度资源（格式、是否反向 Z）、ImGui DX12 后端。相机矩阵、FOV、手性、更新频率、隐藏原生模型的字段都没有实测（外部参考给了偏移线索，见 §7） |

---

## 6. 待验证清单（按优先级）

0. **世界射线查询**（P0，和相机、深度同级）：游戏自己的射线测试函数——朝地面、墙、敌人各射几次，对照屏幕，确认线程。
1. 逐帧玩家坐标，抓住跳变帧：同帧玩家单帧位移是否 ≈ `(+56,0,+40)`；同时记录所有敌人是否同帧得到相同位移。固定同一个 `EnemyIns*`。**同时读 Havok 位置（`PhysicsModule+0x70`）、块局部位置（`PlayerIns+0x6C0`）和块 id（见 §7），看 `Havok − Block` 在哪一帧变化，变化量即原点移动量。**
2. 玩家真实跑过的距离 vs 95.9 米；近距离（约 5 米）可见敌人的坐标差。
3. `readPlayerVitals` 在真机上跑通（`+0x1E508`）；`PhysicsModule` 虚表与 owner 校验；朝向四元数与跑动方向。
4. 一次真实命中的 `0x14044CCA0` 上下文（`+0x54 != 0` 的调用，dump `RDX` 前 `0x1C0` 字节，加线程和返回地址）；`+0x48` 是否等于扣血量。
5. 敌人侧伤害模块的对应函数；敌人最大血量字段、死亡/卸载识别；`team_type` 在召唤物、友方、Boss 上的值。
6. 输入路径计数钩子。
7. 渲染：D3D12 命令队列、深度；着地标志。相机与隐藏模型按 §7 的线索验证。
7a. 读 `ChrIns+0x6C`（玩家、各类敌人、召唤物、友方）与 `+0x1B0`（加载状态），确认 `+0x68` 是 `chr_type`。
8. 所有固定 RVA 改成签名或 RTTI 查找。
9. 方块碰撞方案：注册进游戏物理，还是自己拦截移动。
10. 着地标志：偏移已有（§7.2），见 15。
11. 敌人枚举完整性（见 §7.2，`+0x1E270` 只是 `chr_sets[115]`）：遍历 `chr_sets[0..196]` 的非空项，核对 `ChrSet` 内部布局（`+0x08` 是容量还是数量、数组起点）；读 `chr_inses_by_distance`（确认偏移 `+0x1F1D0` 还是 `+0x1F1D8`、`DLVector` 布局、距离相对谁）；与敌人总数对照。
12. 隐藏模型：`PlayerIns+0x648`（`chr_asm_model_ins`）之下的部件数组、`CSModelIns.model_disp_entity.disp_flags1` 的偏移，用 RTTI 做只读遍历确认，再谈写入。
13. 相机：读 `WorldChrMan+0x1ECE0`（`chr_cam`）→ `ChrCam+0x10` 矩阵、`+0x50` fov；转头、前进验证手性和单位；记录每帧是否更新（只狼是隔帧 30 Hz）；瞄准、死亡镜头时 `camera_type` 与活动相机的关系。
14. 射线：用签名找 `CSPhysWorld::cast_ray` 或 `cast_shape` 的地址（不要用别的补丁的 RVA）；确认 `filter` 含义、调用线程、是否能拿到法线和命中对象。
15. 读 `PhysicsModule+0x92/+0x93/+0x1D0/+0x1D1`，跳起、落地时看是否按预期翻转。
16. 物理模块 `+0x120..+0x128` 是否真是线速度：对比 `Δ位置/dt`（源码里该处叫 `gravity`）。

---

## 7. 外部参考（线索，不是证据）

来源（2026-10 读取，网页摘要，非逐行原文）：
- `Dasaav-dsv/fromsoftware-rs`（`crates/eldenring`，Rust，MIT / Apache-2.0 双许可）：`cs/chr_ins.rs`、`cs/camera.rs`、`position.rs`。
- `Dasaav-dsv/erfps2`（`src/player.rs`）：第一人称与隐藏玩家模型。
- `crosire/reshade`：通用 D3D12 深度检测算法（不含 ER 专属结论）。

README 没写目标游戏版本，仓库同时含 Nightreign，偏移可能针对较新的补丁。**我们这版 exe 上每一项都要实机验证，验证前一律按 C 级对待。**

| 线索 | 来源给出的内容 | 我们要做的验证 |
|---|---|---|
| `ChrIns` 字段 | `+0x08` 句柄、`+0x38` block_id、`+0x60` npc_param_id、`+0x64` npc_id、`+0x68` chr_type、`+0x6C` team_type(u8)、`+0x70` p2p 句柄、`+0x80` chunk_position(float4)、`+0x90` initial_position、`+0x180` last_hit_by、`+0x188` character_id、`+0x190` modules、`+0x1B0` load_state | `+0x64`、`+0x80`、`+0x190` 已和我们的实测吻合；其余待读 |
| 相机 | `CSCamera` 有 4 个 `CSPersCam` 指针；`CSCam`：`+0x10` 4x4 矩阵（行：右、上、前、位置），`+0x50` fov，`+0x54` aspect，`+0x58` near，`+0x5C` far | 哪个 `pers_cam` 是活动的；是否与 RTTI `CSCameraImp`（`0x2A2AFF0`）是同一个单例；手性、更新频率（只狼是隔帧 30 Hz）源码**没写**，必须实测 |
| 坐标 | `BlockPosition` 与 `HavokPosition` 都以米为单位，可互相做位移；Havok 空间是碰撞和相机共用的空间 | 玩家、敌人、相机是否都在同一个 Havok 空间；整数跳变是否是 Havok 原点重定位（§4 的检验）。源码**没有**描述原点何时变化 |
| 隐藏模型 | `disp_flags1 = disp_flags1 & !1 \| state`，`0x100000A1` 可见，`0x100000A0` 隐藏且保留阴影；另一条路是写 `PlayerIns.base_transparency`（魔数） | 先只读遍历部件，确认 `disp_flags1` 当前值；`base_transparency` 的偏移源码没给，别信其他来源写的 `+0x24C` |
| 深度 | ReShade 的做法：跟踪主绘制 pass 的 DSV，拷贝到可读纹理 | ER 的格式和是否反向 Z 必须自己测（只狼是 `R32G8X24_TYPELESS` + 反向 Z） |

这些仓库**没有**给出：世界射线查询、敌人侧受击的调用方式、方块碰撞、D3D12 命令队列捕获、输入路径（`pad.rs` / `mouse_man.rs` 未核对）。

### 7.1 第二批线索（2026-10，核对 `fromsoftware-rs` 的 `physics.rs`、`world_chr_man.rs`、`chr_ins.rs`）

可由源码字段排布推算的（`unkXXX` 名字带偏移，**推算，非明文**）：

| 项 | 推算结果 | 状态 |
|---|---|---|
| `PlayerIns.chr_asm` | `+0x638` | 待读 |
| `PlayerIns.chr_asm_model_res` / `chr_asm_model_ins` | `+0x640` / `+0x648`，与逆向方给的一致 | 待读 |
| `PlayerIns.block_position` | `+0x6C0`，`BlockPosition { x, y, z, yaw }`（**块局部坐标**，不是全图坐标），紧跟 `current_block_id` | 待读 |
| `base_transparency` | 在 `ChrIns` 里（不是 `PlayerIns`），偏移**未给出**；别的来源写的 `+0x24C` 没有依据 | 未知 |

`CSChrPhysicsModule` 的字段名（源码）：`position`、`last_update_position`、`standing_on_solid_ground`、`touching_solid_ground`、`orientation`（四元数）、`interpolated_orientation`、`orientation_euler`、`is_falling`、`is_touching_ground`、`gravity_disabled` 等。**源码摘要没有给偏移**；逆向方转述的 `+0x92`、`+0x93`、`+0x1C8` 来自 TGA 表，未核对。我们实测的 `+0x70` 位置与 `+0x50..+0x5C` 朝向与字段名一致。

`WorldChrMan`（源码字段，无偏移）：`player_chr_set`、`ghost_chr_set`、`summon_buddy_chr_set`、`debug_chr_set`、`open_field_chr_set`、`chr_sets[196]`、`main_player`、`world_area_chr[28]`、`world_block_chr[192]`、`chr_inses_by_distance`、`chr_inses_by_update_priority`。转述的 `WorldChrMan+0x10EF8`（`player_chr_set`）与 `FieldArea→+0x20→+0x18→+0x0`（活动相机）来自 TGA，未核对。

有用的方法：
- **原点跳变的干净检验**：块局部坐标在同一块内不受 Havok 重定位影响，所以 `Havok(PhysicsModule+0x70) − Block(PlayerIns+0x6C0)` 在没跳变时恒定，跳变那一帧会变，变化量就是原点移动量。注意跨块时块局部坐标自己也会跳（`current_block_id` 变）。
- `BlockPosition.yaw` 是现成的朝向角，可与 `PhysicsModule` 四元数交叉验证。
- `chr_inses_by_distance` 是游戏自己按距离排的列表，若带距离，就是在游戏自洽的坐标里算出来的，比我们自己做坐标差可靠。内部结构未知。

### 7.2 第三批线索：偏移推算（`fromsoftware-rs` 的结构体字段排布，**推算，非明文**）

方法：Rust 结构体里 `unkXXXX` 的名字带偏移、数组大小已知，据此向前后推。用 `net_chr_sync`（推算 `WorldChrMan+0x1E5E0`）校验：它与我们实机发现的 `NetChrSetSync` 位置吻合，所以这套推算方法可信。

| 项 | 推算偏移 | 备注 |
|---|---|---|
| `WorldChrMan.main_player` | `+0x1E508` | 与逆向方给的一致，待读 |
| `WorldChrMan.net_chr_sync` | `+0x1E5E0` | **与实机一致**（§2 的"网络同步表"） |
| `WorldChrMan.chr_sets[196]` | `+0x1DED8` 起，每项 8 字节指针 | 我们的敌人容器 `+0x1E270` 正好是下标 115 |
| `WorldChrMan.chr_cam` | `+0x1ECE0`（`ChrCam*`） | `ChrCam` 的第一个成员是 `CSPersCam`，所以 `ChrCam+0x10` 是矩阵、`+0x50` 是 fov；另有 `ex_follow_cam`、`aim_cam`、`dist_view_cam` 与 `camera_type`，瞄准或死亡镜头时活动相机不是 `pers_cam` |
| `WorldChrMan.chr_inses_by_distance` | `+0x1F1D0`（`DLVector`，条目 `{ chr_ins, distance: f32, _unk }`，16 字节） | 逆向方给的是 `+0x1F1D8`，可能差一个分配器指针，需看 `DLVector` 布局；"距离相对谁"源码没写 |
| `WorldChrMan.player_chr_set` | `+0x10EE0` | TGA 转述 `+0x10EF8`，可能是它内部的字段 |
| `CSChrPhysicsModule` | `+0x08 owner`、`+0x20 data_module`（指向 `CSChrDataModule`）、`+0x50 orientation`、`+0x60 interpolated_orientation`、`+0x70 position`、`+0x80 last_update_position`、`+0x92 standing_on_solid_ground`、`+0x93 touching_solid_ground`、`+0x1D0 is_falling`、`+0x1D1 is_touching_ground`、`+0x1D5 gravity_disabled` | `+0x50/+0x70/+0x80` 与我们实测吻合；着地各字节待跳跃实测 |

**与我们实测的差异**：源码里物理模块 `+0x120` 的 `F32Vector4` 叫 `gravity`，而我们量到的"线速度 `+0x120..+0x128`"在走动时变化（`(0,−0.16,0)` → `(−3.13,−5.70,0.98)`），更像速度。**"这就是线速度"没有被证明**，使用前需验证它是否等于 `Δ位置/dt`。

**敌人枚举**：`chr_sets[196]` 里的每一项都是一个 `ChrSet`。只读下标 115 会漏掉其余的。可行做法：遍历 `chr_sets[0..196]` 的非空项；或读 `chr_inses_by_distance`（完整列表，带游戏自己算的距离）。`ChrSet` 内部布局（`+0x08` 是容量还是数量，数组起点）要核对；下标 115 与容量 115 同为 115 是巧合还是关联，也要确认。

**射线（`fromsoftware-rs` `havok_man.rs`、`erfps2` `raycast.rs`）**：
- `CSHavokMan` 的 `phys_world` 在 `+0x98`（推算）。
- `cast_ray(world, filter: u32, origin, end, &mut out_pos, owner: &PlayerIns) -> bool`：**只返回命中点，没有法线，也没有命中对象**；放方块、射箭都不够。
- `erfps2` 用 `cast_shape` + `hknpHit { pos, normal, segment, filter, body_id, body }` 才有法线和命中对象，但要构造碰撞收集器。
- 转述的 RVA（`0x187f860`、`0xc5a1c0`、`0xc71e00`）在抓到的源码里没有数值，且属于别的补丁，**不能直接用**。
- `filter` 取值含义、调用线程要求，源码都没写。

**隐藏模型与第一人称（`erfps2` `player.rs`）**：`HEAD_DMY_ID = 907`；部件下标 `[0, 2, 6, 21, 22, 23, 24, 25]`（脸、头盔、头发、眼睛）；`chr_asm_model_res` 的 `+0x60`（耳，bit 3）、`+0x51`（兜帽，bit 11）、`+0x58`（兜帽变体，bit 5）。`GET_DMY_POS_RVA` 的数值未核实（转述 `0x3e96b0`）。

**输入（`pad.rs`）**：`UserInputKey`：`Attack = 7`、`Guard = 9`、`Jump = 14`，鼠标位移 `4`、`5`；游戏层有 `key_assign.mouse_button_states_map` 一类的鼠标按键状态。源码**没有**写底层是 DirectInput 还是 RawInput，也没有 `poll_digital_input` 的地址；拦截点偏移未知。

**仍无来源的结论**：Havok 原点按整数步长平移（源码没解释，§4 的检验照做）；ReShade "验证了 ER 反向 Z 与格式"；TGA 的 `NoDamage`（`+0x19B` bit 1）。
