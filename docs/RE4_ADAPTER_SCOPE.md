# RE4R 适配器范围与前置重构决策

> 对应 issue **#177（生化危机4重制版适配器 —— 范围、前置重构清单与 REFramework 接入方案）**。
> 本文只记录**范围、决策和状态**，不写实现；接新游戏的通用流程仍以
> [PORTING_PLAYBOOK.md](PORTING_PLAYBOOK.md) 为准，宿主接口逐项清单见
> [REVERSE_INTERFACES.md](REVERSE_INTERFACES.md)，模式仲裁见 [CONTROL_POLICY.md](CONTROL_POLICY.md)。
>
> **状态口径**：本文区分**已交付**与**未交付**。已交付项写明承载它的 `refactor/` 分支和草稿 PR；
> 未交付项只写决策、阻塞原因和依赖顺序。**已交付的四个 `refactor/` 分支都还没有合入 `main`**，
> 在 `main` 的工作区里看不到这些文件，以分支为准；合入后本文即为现状说明。
> 本轮实际交付四项（P1–P4，见 §0）：`PhysicsFeature` 能力位与降级物理端口（#188）、
> D3D12 无宿主原语下沉（#189）、`IRigRenderer` 接缝（#190）、`FirstPersonRig` 抽取（#191），
> 全部是**草稿 PR，未合并**。
> `IHostRuntime` / `Dinput8Runtime` / `ReframeworkRuntime`（P5）**阻塞、未交付**：分支
> `refactor/host-runtime` 停在 `20fb4f2`，没有 PR，未通过独立验证，六条阻塞项见 §5.4，顺延到下一晚。

---

## 0. 本轮夜间重构的阶段状态

> 这里的 P1–P6 是**本轮夜间重构的阶段编号**，与 #177 里的 P0/P1/P2（范围分层，见 §3）不是同一套编号。

| 阶段 | 内容 | 分支 | PR | 状态 |
|---|---|---|---|---|
| P1 | `PhysicsFeature` 能力位 + `DegradingPhysicsPort` 降级物理端口 | `refactor/physics-capabilities` | #188（draft） | 已交付，未合并 |
| P2 | 六个游戏无关 D3D12 模块下沉到 `adapters/common/d3d12_rig` | `refactor/d3d12-rig-primitives` | #189（draft） | 已交付，未合并 |
| P3 | `IRigRenderer` 接缝 + `ErRigRenderer` 转发 | `refactor/rig-renderer-interface` | #190（draft） | 已交付，未合并 |
| P4 | `FirstPersonRig` + `IFirstPersonCamera` | `refactor/first-person-rig` | #191（draft） | 已交付，未合并 |
| P5 | `IHostRuntime` / `Dinput8Runtime` / `ReframeworkRuntime` | `refactor/host-runtime`（停在 `20fb4f2`） | 无 | **阻塞，未交付**（§5.4） |
| P6 | 本文档：交付状态修订 | `docs/re4-adapter-scope` | — | 已交付（本文） |

“已交付”指分支上有实现提交并开了草稿 PR，**不等于合入 `main`**；`main` 上看不到这些文件，以分支为准。

## 1. 结论

**核心不需要重写，但适配器层要先补共享基础设施（P0 四项，见 §3.1；其中三项已作为草稿 PR 交付，
`IHostRuntime` 仍阻塞）。**

- `mc::Session`、玩法数学、`mc::rig` 几何、HUD 布局、`ControlPolicy`、资产管线都是平台无关的，
  RE4R 只需要换一套 ports 实现。
- 真正的阻塞不在玩法规则，而在：**加载模型**（REFramework 插件，不是 `dinput8.dll` 代理）、
  **宿主取数方式**（managed object 反射，不是 AOB + 裸偏移）、以及 RE4R **不具备的移动/物理能力**
  （无跳跃、速度写入基本无效、动态碰撞体注册不现实）。
- 法环适配器里已经有一整套可复用的 D3D12 基础设施（Present / ResizeBuffers /
  ExecuteCommandLists / CreateDepthStencilView 钩子、Steve 渲染、深度遮挡、第一人称手臂、方块渲染）。
  RE4R 同为 D3D12，这是最大的复用点——但复用前必须先把它从 `erov::` 里抽出来。

## 2. 与法环 / 只狼的关键差异

| 维度 | 法环 / 只狼 | RE4R |
|---|---|---|
| 加载方式 | `dinput8.dll` 代理 | **REFramework 插件**（`reframework/plugins/`）；`dinput8.dll` 已被 REFramework 占用，不能再用代理 |
| 图形 API | D3D12（法环）/ D3D11（只狼） | D3D12（可复用法环渲染层） |
| 宿主取数 | AOB 特征码 + 内存偏移 | REFramework managed object 反射（`app.CharacterManager`、`via.Transform`、`app.HitController`） |
| 反篡改 | 无 | Denuvo + Enigma Protector；**不做任何绕过**，只走 REFramework 支持的插件路径 |
| 移动/物理 | 有跳跃、可写速度、Havok 射线 | 无跳跃；角色移动由动画/根运动驱动，速度写入基本无效；动态碰撞体注册不现实 |
| 输入 | DirectInput 设备包装 | RawInput / REFramework input，现有抑制实现不能直接复用 |
| 视角 | 第三人称（第一人称自绘） | 越肩视角；第一人称要写相机，必须在渲染前、每帧覆盖、可还原 |

## 3. 范围（#177 的 P0 / P1 / P2）

### 3.1 P0 —— 不做就没法开工

| 项 | 决策 | 状态 |
|---|---|---|
| D3D12 渲染后端下沉 | 先搬无宿主依赖的原语、定 `IRigRenderer` 接缝；`SteveRenderer` 主体推迟（见 §5.2） | 原语已下沉（#189 `refactor/d3d12-rig-primitives`）、接缝已定（#190 `refactor/rig-renderer-interface`）；`SteveRenderer` 主体推迟。两者都是草稿 PR，未合并 |
| HostRuntime / Loader 层 | 定义 `IHostRuntime`，两个实现：`Dinput8Runtime`（法环/只狼）、`ReframeworkRuntime`（RE4R） | **阻塞，未交付**：分支 `refactor/host-runtime` 停在 `20fb4f2`，没有 PR；六条阻塞项见 §5.4，顺延到下一晚 |
| 能力位扩展 | 物理能力不进 `HostFeature`，另立 `PhysicsFeature`（见 §5.1） | 已交付（#188 `refactor/physics-capabilities`，草稿 PR，未合并） |
| 第一人称相机契约定型 | 进入/退出、眼睛位置、FOV、隐藏头部、恢复原相机；相机姿态策略抽成通用 `FirstPersonRig`（见 §5.3） | 已交付（#191 `refactor/first-person-rig`，草稿 PR，未合并）：`IFirstPersonCamera` + `FirstPersonRig`；RE4R 侧相机写入仍待 spike（§6.3） |

### 3.2 P1 —— 开工前强烈建议

- **输入层抽象**：定义 `IInputSource`（原始键鼠事件、鼠标增量、战斗键抑制）；RE Engine 走
  REFramework input / RawInput，不是 DirectInput 设备包装。
- **实体身份通用化**：把法环实体表抽成通用 `EntityRegistry`（稳定 `EntityId` ↔ 宿主对象）；
  RE Engine 用 managed object 句柄，**宿主指针/句柄不得直接当 `EntityId`**。
- **HUD 绘制共享化**：`include/mc/contracts/hud_renderer.hpp` 目前只有接口、没有实现使用；
  抽 `HudPainter(ImDrawList, atlas, HudEngine)`，法环/只狼/RE4R 共用。
- **移动/物理能力降级**：跳跃、冲刺、鞘翅、摔落伤害、速度写入全部按能力开关；
  `PhysicsFeature` 能力位与 `DegradingPhysicsPort` 已在 #188 交付；RE4R 无跳跃的替代方案
  （蹲伏/攀爬映射或直接关闭）仍需产品决策。
- **世界射线与方块碰撞范围决策**：`via.physics.Raycast` 可做；动态方块注册进 RE Engine 物理基本不现实。
  先定“方块只做视觉 + 自有层拦截”还是第一版砍掉建造玩法。
- **构建/打包/测试**：增加 `MC_ENABLE_RE4` 构建轴并**真正接进 CI**（当前 CI 不配置它，见 §5.4 第 5 条）；
  产物是 REFramework 插件 DLL，不是 `dinput8.dll`；`test_session_capability_degrade.cpp` 已随 #188 交付，
  RE4R 侧仍需补用例；`tests/test_adapter_standards.py` 增加 REFramework 插件路径检查。

### 3.3 P2 —— 边做边补

- 配置、热键、日志、存档/背包持久化的共享化。
- 版本策略：RE4R 多版本（1.0 / 1.1 / 1.5+ Separate Ways）用 REFramework 反射 + 版本校验，
  **不做裸 AOB 扫描**。

### 3.4 第一版范围（产品决策）

只做 **Steve 模型 + HUD + 近战命中 + 方块视觉放置**。鞘翅、生存、弓箭暂不做——
RE4R 的物理模型撑不起这些。方块是否真的能挡住玩家，见 §3.2 的范围决策。

### 3.5 不需要动的部分

`mc::Session` 玩法数学、`mc::rig` 几何、HUD 布局、`ControlPolicy`、资产管线、核心单测。

## 4. REFramework spike：本轮推迟，四项验收不变

#177 原本建议“先花 1–2 天写一个最小插件，再抽抽象”。**本轮重构推迟 spike**，原因有两条：

1. spike 需要真实的 RE4R + REFramework 运行时，重构环境里没有，不能凭纸面结论写插件入口；
2. #177 自己写明：“四件事都通过后再抽 `d3d12_rig` 和 `IHostRuntime`，否则容易把抽象抽错层。”
   没有真机证据就去抽，等于把猜测固化成接口。

所以本轮只做**不依赖 RE Engine 行为**的部分：无宿主类型的渲染原语、接口接缝、能力位、
第一人称相机姿态逻辑、文档。凡是依赖实机标定的（深度约定、ImGui 归属、插件入口、每帧回调时机）
一律留给 spike。

P0 四项里三项已作为草稿 PR 交付（§0 的 P1–P4，D3D12 一项拆成 P2/P3 两个 PR），**只有 `IHostRuntime` 未交付**：分支
`refactor/host-runtime` 停在 `20fb4f2`，没有 PR，六条阻塞项见 §5.4。spike 仍是开始写 RE4R
适配器的**硬门**——`ReframeworkRuntime` 的插件入口、设备/队列来源和 ImGui 归属都要等它回答；
`FirstPersonRig` 虽已抽取，RE4R 的相机写入也必须先在真机上标定。

**四项验收（全过才允许开始写 RE4R 适配器）：**

1. 插件能被 REFramework 加载，能拿到 D3D12 设备/队列和 ImGui 上下文；
2. 能读到玩家位置、朝向、血量；
3. 能隐藏原生角色模型；
4. 能画出一个 ImGui 矩形，证明渲染路径可用。

spike 顺带回答 #177 的“待确认”：REFramework 插件 API 的目标版本与许可证；ImGui 上下文归属
（复用 REFramework 的还是自建，二选一）；RE4R 深度缓冲能否稳定拿到（Steve 遮挡依赖它）。

## 5. 决策记录与交付状态（本轮重构）

### 5.1 HostFeature vs PhysicsFeature：物理能力不进 HostFeature

`HostFeature`（`include/mc/contracts/host_gameplay.hpp`）回答的是“**宿主能提供哪些游戏数据和动作**”：
血量、敌人枚举、输入抑制、受伤事件、第一人称、着地标志。它有三个约束：

- 每一项在 [REVERSE_INTERFACES.md](REVERSE_INTERFACES.md) 里都有一节，测试
  `test_every_host_feature_is_documented_in_the_reverse_interface_list` 逐位校验；
- `Session::missingHostFeatures()` 用它报告缺什么，语义是“玩法会降级”；
- 每一项都有无害默认实现，适配器只声明**在真机上验证过**的位。

#177 曾建议把 `WorldRaycast`、`BlockCollision`、速度写入、跳跃、摔落也塞进 `HostFeature`。
**本轮否决了这个做法**，改为在 `include/mc/contracts/physics_adapter.hpp` 里另立 `PhysicsFeature` 位图：

- 这几项已经在 `IPhysicsAdapter` 上（`raycastWorld` / `createBlockCollider` / `destroyBlockCollider` /
  `applyLinearImpulse` / `setLinearVelocity`），属于**写宿主物理**，线程约束、崩溃风险和
  “只读数据”完全不同；
- `HostFeature` 的 1:1 文档/测试约束不适合再塞 5 个物理位，混在一起会让“缺数据”和“不能写物理”
  用同一套降级语义；
- RE4R 的关键差异（无跳跃、速度写入基本无效、动态碰撞体不现实）正好全是**物理能力**，
  单独成表才能让适配器一次说清“我能做哪些物理”。

`GroundedFlag` 留在 `HostFeature`：它是**读**，且已有“垂直速度小”的估计值兜底，不属于物理写入。

**交付状态**：该决策已在 #188（`refactor/physics-capabilities`，草稿 PR，未合并）落地——
`include/mc/contracts/physics_adapter.hpp` 增加 `PhysicsFeature`，新增
`include/mc/degrading_physics_port.hpp` 的 `DegradingPhysicsPort`，`Session` 通过它把缺失能力
降级成无害默认值，`tests/test_session_capability_degrade.cpp` 覆盖这些分支。

**Session 的降级规则**：驱动滑翔前必须查物理能力位。宿主不支持速度写入时**不要每帧写一个无效值**
（在 RE4R 上会表现为“看起来在滑翔、实际不动”），而是按降级处理，并把缺失能力报到日志/HUD。
改动前 `src/session.cpp` 是无条件调用 `setLinearVelocity` 的，这正是 #177 点出的坑。

### 5.2 D3D12 抽取：原语下沉 + IRigRenderer 接缝，SteveRenderer 主体推迟

**重构前**：D3D12 代码全部住在法环适配器里，命名空间是 `erov::`：

- `adapters/eldenring/src/render/`：`d3d12_util`（`Rel` / `UploadBuffer` / `UploadMesh`）、
  `d3d12_pipeline_builder`（root signature + PSO 描述，纯部分可单测）、`d3d12_shader_lib`
  （运行时 `D3DCompile` 内嵌 HLSL）、`steve_mesh_factory`（CPU 网格，平台无关）、
  `steve_texture_cache`、`steve_pipeline`、`steve_types`（故意不含 `d3d12.h`，全平台可测）；
- `steve_renderer_d3d12.*`、`overlay_hook.cpp`、`overlay_d3d12.*`。

**本轮搬走的部分**（#189 `refactor/d3d12-rig-primitives`，草稿 PR，未合并；目标路径
`adapters/common/d3d12_rig`，与 `adapters/common/d3d11_rig` 并列）：

- 上述 `render/` 下的无宿主依赖原语；
- 去 ER 化：`erov::` → `mc::d3d12`，`erov::LogFn` → 中立日志回调，去掉 ER 的配置、纹理路径
  和 shader include 路径（`render/steve_types.hpp` 这类相对 include 必须改成公共层的路径）。

**`IRigRenderer` 接缝**：新增一个后端无关接口，目标是 D3D11 与 D3D12 实现都满足它，表面与
`adapters/common/d3d11_rig/include/d3d11_rig/rig_renderer.hpp` 对齐
（`init` / `setSkin` / `setSceneDepth` / `setDepthConvention` / `setHeldItem` / `setBlocks` /
`draw` / `drawFirstPerson`）。
接口落在 `include/mc/rig/rig_renderer.hpp`，法环侧的 D3D12 实现是
`adapters/eldenring/src/render/er_rig_renderer.*`（#190 `refactor/rig-renderer-interface`，草稿 PR，未合并），
它转发到现有的 `SteveRenderer`。**D3D11 侧尚未接这个接口**，仍是 `d3d11_rig` 自己的 `RigRenderer`。
适配器和 `Session` 只依赖接口，不依赖具体后端。

**深度约定保持参数化**：公共层只接受 `mc::rig::DepthConvention`，ER 的
`depth_const = 0.0501`（reverse-Z、近平面常数）**不得带进公共层**。RE Engine 的深度格式和
Reverse-Z 行为必须实机标定，未标定前遮挡自动关闭。

**推迟的部分**：`steve_renderer_d3d12.cpp` 主体（682 行）与其头文件（261 行）本轮不搬。
它仍带大量法环玩法假设：盔甲层贴图、方块与地形裂纹网格、背包 3D 预览、深度标定模式、
ER 的 shadow/steve 类型。按 §4 的理由，spike 之前抽它会把“ER 需要的渲染能力”误当成“所有宿主需要的”，
而 RE4R 第一版只需要 Steve + HUD + 近战 + 方块视觉。等 spike 证明 RE4R 到底需要哪些能力，再决定拆法。

`overlay_hook.cpp`（859 行，Present / ResizeBuffers / ExecuteCommandLists /
CreateDepthStencilView）与 `overlay_d3d12` 的归属等 HostRuntime 落地时再定：REFramework 自带设备/队列
和 ImGui 上下文，Present 钩子未必还需要，同样等 spike 定。

### 5.3 第一人称：契约与相机策略已交付（P4），手部姿态未抽取

#177 要求契约定型，因为 `HostFeature::FirstPersonCamera` 只有枚举位，`IHostGameplay` 上没有
进入/退出方法，注释还写着“接口待第一个实现该特性的游戏定型”。

**契约要表达的行为（五项）**：进入/退出、眼睛位置、FOV、隐藏头部、恢复原相机。本轮已在
#191（`refactor/first-person-rig`，草稿 PR，未合并）上落成两个头文件：

- `include/mc/contracts/first_person_camera.hpp`：`IFirstPersonCamera`，宿主侧只实现
  `save()` / `writeEye()` / `writeFov()` / `restore()` / `hideNativeHead()`；
- `include/mc/rig/first_person_rig.hpp`：`FirstPersonRig`，与宿主无关的**相机策略**——眼睛在脚 +
  眼高、站/潜行眼高按帧率无关的指数插值过渡、进入时保存一次、退出时按写入的逆序还原一次、
  只在激活期间写 FOV。

法环侧已接线：`ChrCamFirstPersonCamera` 实现 `IFirstPersonCamera`，`FirstPersonRig` 在
`adapters/eldenring/src/hooks/` 里驱动它。`HostFeature` 的枚举位仍在
`include/mc/contracts/host_gameplay.hpp`；`IFirstPersonCamera` 是新增的独立契约，
没有改动 `IHostGameplay`。

**没有抽取的部分**：法环的手部/持物姿态仍在 `adapters/eldenring/include/eldenring_fp.hpp`
（Minecraft `ItemInHandRenderer` 的挥击/进食/拉弓/三叉戟/盾牌/裸臂姿态、走路 bob、转向 sway、
`HandAnimator`、`SwayFilter`），**本轮没有搬进公共层**。它和上面的相机策略是两件事；
RE4R 第一版是否需要同一套手部姿态，等 spike 之后按需再抽。

**对 RE4R 的含义**：越肩视角意味着第一人称必须**写宿主相机**——在游戏渲染之前改、每帧覆盖、
退出时还原；头部隐藏和 FOV 约定都要单独验证。`FirstPersonRig` 已经就绪，RE4R 只差
`IFirstPersonCamera` 的实现，这部分依赖 spike，不在本轮。

### 5.4 HostRuntime：阻塞，未交付（P5）

**结论：不交付。** 分支 `refactor/host-runtime` 停在 `20fb4f2`，没有 PR；它没有通过独立验证，
按现状不合并，顺延到下一晚重做。

分支上已有的形状（仅作为下次的起点，**不在 `main` 上**）：`include/mc/contracts/host_runtime.hpp`
定义 `IHostRuntime`，`adapters/common/runtime/` 下是 `Dinput8Runtime` 与 `ReframeworkRuntime`，
法环 loader 改为由 runtime 持有入口。目标是适配器只实现 ports 和宿主查询，不再自己写
`DllMain` / `MH_CreateHook` / `PresentDetour`——当前实现还没达到（见第 6 条）。

六条阻塞项（独立验证两轮得出，修复前不得开 PR）：

1. **卸载语义不成立**：`IHostRuntime` 约定空的 `installPresentHooks` / `installInputHooks`
   回调集合会卸载先前安装的钩子，但 `Dinput8Runtime` 只清空私有副本，从不调用卸载。
2. **初始化早于环境门**：`Dinput8Runtime` 在 `LoaderThread` 的 EAC / `steam_appid` 环境门之前
   调用 `LoadHostConfig()`（读配置、起日志），改变了原有顺序。
3. **能力位写死**：`HostRuntimeInfo` 固定 `supports_present_hooks=false` /
   `supports_input_hooks=true`，与实际绑定无关，报告的能力是假的。
4. **shutdown 后使用**：`ReframeworkRuntime::shutdown()` 先调 `api_->shutdown()`，
   随后又经 `log()` 使用 `api_`。
5. **RE4 测试不进 CI**：`MC_ENABLE_RE4` 只 gate 了 `ReframeworkRuntime` 的实现，CI 从不配置它，
   RE4 假 API 测试在 CI 里根本不跑。
6. **生产路径没被覆盖**：法环适配器仍自己安装 present/input detour（`kBindings` 里两个 installer
   是 `nullptr`），runtime 的钩子面只有单测覆盖，没有真机证据。

验收标准沿用 #177：`skills/mc-game-adapter/assets/plugin-entry.template.cpp` 保持 runtime 无关，
只驱动 `mc::Session`。下一晚先修这六条、跑独立验证，再开 PR；`ReframeworkRuntime` 仍要等 spike
（§6.3）。

## 6. 剩余工作

### 6.1 先做：P5 的六条阻塞项（下一晚）

1. `IHostRuntime` + `Dinput8Runtime`：按 §5.4 的六条逐项修复——空的回调集合要真的卸载、配置引导
   放回环境门之后、能力位由绑定推导、`shutdown()` 之后不再碰 `api_`、`MC_ENABLE_RE4` 进 CI 构建
   矩阵、法环生产路径改用 runtime 的钩子面并有真机验证。修完跑独立验证再开 PR。

### 6.2 REFramework spike（硬门，先于任何 RE4R 代码）

2. spike 四项验收（§4）全过后，才允许开始写 RE4R 适配器。

### 6.3 spike 通过后，按依赖顺序

3. `ReframeworkRuntime`：插件入口、D3D12 设备/队列、ImGui 上下文归属、每帧回调（依赖 §5.4 的
   接口先稳定）。
4. `adapters/common/d3d12_rig` 的 D3D12 后端完成：接 `IRigRenderer`（#190），标定 RE Engine 的
   深度约定，再决定 `SteveRenderer` 主体怎么拆（盔甲/方块/背包预览按能力开关）。
5. 第一人称：`FirstPersonRig` / `IFirstPersonCamera` 已在 #191 就绪；RE4R 侧只差
   `IFirstPersonCamera` 的实现——相机写入与还原、头部隐藏、FOV 换算，全部依赖 spike。
6. `EntityRegistry`：managed object 句柄 ↔ 稳定 `EntityId`（句柄不得直接当 id）。
7. `IInputSource`：RawInput / REFramework input；战斗键抑制按宿主路径重写，不复用 DirectInput 包装。
8. `HudPainter`：法环/只狼/RE4R 共用的 `ImDrawList` 绘制层。
9. 构建与打包：`MC_ENABLE_RE4` 构建轴进 CI（当前 CI 不配置它，见 §5.4 第 5 条）、REFramework
   插件 DLL 产物、插件路径的标准检查。
10. 能力降级单测：`test_session_capability_degrade.cpp` 已随 #188 交付；RE4R 侧补适配器级用例
    （例如“没有速度写入时不进入滑翔”）。

仍需产品决策：方块是否要真的挡住玩家（视觉 + 自有层拦截，还是第一版砍掉建造）；
无跳跃宿主的移动映射（蹲伏/攀爬，或直接关闭跳跃相关玩法）。

## 7. 合规规则

- **仅单机**，不涉及任何在线/反作弊场景。
- **不绕过 Denuvo、Enigma Protector 或任何所有权校验**，不写 anti-tamper bypass；
  只走 REFramework 官方支持的插件加载路径。
- **不使用 `dinput8.dll` 代理**：RE4R 上它已被 REFramework 占用。
- **不提交任何游戏原始资产、提取资源或反编译代码**：皮肤与图集从用户本地的 Minecraft
  client jar 生成，只留在游戏目录；反编译产物放在仓库之外。
- 逆向只在用户拥有的游戏副本上做，**以只读为主**；任何写宿主内存的地方都必须有
  类名/数值校验和恢复路径，失败时报告而不是假装成功（见
  [REVERSE_INTERFACES.md](REVERSE_INTERFACES.md) 的交付物清单）。

---

参考：issue #177；[PORTING_PLAYBOOK.md](PORTING_PLAYBOOK.md)（接新游戏的阶段与踩坑）、
[REVERSE_INTERFACES.md](REVERSE_INTERFACES.md)（宿主接口逐项清单）、
[ARCHITECTURE.md](ARCHITECTURE.md)（分层与契约）、[CONTROL_POLICY.md](CONTROL_POLICY.md)（模式仲裁）。
