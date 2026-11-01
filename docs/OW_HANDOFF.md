# 🛡️ Overwatch (Doomfist) 第一人称护手系统 —— 深度交接文档

> **PR 编号**：[PR #128 (GitHub)](https://github.com/icebear0828/mc-core/pull/128)  
> **分支**：`feat/ow-only`（跟踪 `fork/feat/ow-only`，已合并并推送到远端）  
> **最新 Commit**：`7563659 feat(eldenring): fix OW texture descriptor slot, mask native actions, and add attack animations`  
> **读者定位**：接续开发本模块的下一任 Agent 或多模态/系统工程师。

---

## 0. 核心进展与现状总结 (Status Overview)

用户在实测后先后给出了关键反馈：
1. **“手持是没问题了”**：证明我们基于线性混合蒙皮 (LBS) 及 UE4 原生 `Camera` 逆变换计算的第一人称右下角入镜握拳手持姿态在游戏内完全生效，手持视口和空间坐标准确无误。
2. **“没贴上原版 ow 的贴图”**：发现模型在游戏内呈现灰黑色无贴图状态。现已查明为描述符槽位覆写 Bug 并彻底修复。
3. **“目前你甚至都没有把原版法环的动作屏蔽，就像 mc 和 cs 那样屏蔽掉”**：已定位为 `WantSuppress()` 缺失 `ow_mode` 导致 DirectInput8 鼠标拦截未激活，现已完全屏蔽法环原版挥刀与动作后摇，并隐藏第一人称法环身体。
4. **“美区没有任何攻击动作”**：左键手炮直拳突刺、右键火箭蓄力重拳及对应时序状态机已全面接入并经 TDD 严格验收。
5. **“先推送 pr 吧，目前还是没和其他模式区分开来”**：已推送到 GitHub 并创建 **PR #128**。
6. **（本轮）彻底解耦**：OW 现在拥有独立的构建宏、渲染器类、着色器、描述符堆和配置文件，与 CS2 / MC 完全正交。三种构建组合（CS+OW、仅 OW、仅 CS、仅 MC）均已编译验证。

### 0.1 本轮完成的解耦改造

| 层 | 改造前 | 改造后 |
|---|---|---|
| 构建宏 | 整块 OW 代码位于 `#if ENABLE_CS2_VIEWMODEL` 内 | 新增独立 `option(ENABLE_OW_VIEWMODEL ... ON)`；`ow_renderer_d3d12.cpp` 仅在 OW 开启时编译 |
| 渲染器 | `CsRenderer g_ow` | 独立 `erow::OwRenderer`（`src/ow_renderer_d3d12.{hpp,cpp}`），自带根签名 / PSO / 深度缓冲 / VB·IB |
| 着色器 | 复用 12 贴图 CS 着色器 | 独立 3 贴图 PBR 着色器（albedo / normal / ORM），光照与 CS 管线对齐 |
| 描述符堆 | 借用 CS 堆 slot 48–63 | 独立 `g_ow_srv_heap`（3 个 SRV，shader-visible） |
| 配置 | 混在 `mc_er_steve.txt` | 独立 `mc_er_ow.txt`：`ow_mode`、`ow_fov`、`ow_scale`、`ow_offset_x/y/z` |
| 纯逻辑 | 复用 `eldenring::cs::CSViewModelAnimator` | 独立 `eldenring::ow::OwAnimator` + `parseOwModel`（`include/ow_viewmodel.hpp`），可单测 |
| 热键 | 无宏保护 | OW 热键与攻击触发由 `#if ENABLE_OW_VIEWMODEL` 保护 |
| 音效 | 回退 `cs.knife.slash` / `cs.knife.hit` | 原版 Bink Audio 解码为 44.1 kHz WAV，注册 `ow.cannon.fire` / `ow.punch.release` |
| 右键位移 | 只有视口动画，玩家本体不动 | `stepOwRocketPunch` 在 state 3 推力段（0.25–0.60 s）沿相机水平前向写入位置，并用 `CastStaticRay` 在腰/胸两个高度夹紧，默认 4 m，可用 `ow_dash_distance` 调 |

### 0.2 模式隔离修复（2026-10-31）

实测反馈“Shift 没屏蔽、三个模式没分开”，逐项 trace 到的根因与修复：

| 根因 | 修复 |
|---|---|
| `ow_keys` 只映射 `VK_SHIFT`（DIK 0x2A），**右 Shift（0x36）仍传给游戏** | OW 键表加入 `VK_LSHIFT` + `VK_RSHIFT`，两个物理 Shift 都屏蔽 |
| 通用 MC 近战/挖掘/F 换手没有 `mc_on` 门控，OW 左键会同时触发 MC 挥击、F 会换手 | 近战条件改为 `mc_on || CS 刀`；挖掘与 F 换手只在 `mc_on` 下执行 |
| CS 构建在 overlay 初始化时强制 `cs_mode=true` 并 `ow_mode=false`，顶掉 OW 配置 | 初始化只在 `ow_mode` 未激活时自动进 CS |
| OW 模式没屏蔽滚轮、R、1–4，原生快捷栏/道具仍会响应 | OW 分支屏蔽 `MouseWheel`，键表加入 `R` 与 `1`–`4` |
| MC 冲刺状态在 OW/CS 下仍每帧更新 | `player.sprint` 只在 `mc_on` 下更新 |

回归测试：`HotkeyDispatcherTest.OwModeDoesNotLeakMcGameplay`、`OwModePlaysAuthenticSoundEvents`、`EldenRingControlPolicy.OwMode*`，全量 1247/1247 通过。

### 0.3 骨骼动画完整接入（2026-11-01）

实测反馈"射击没问题，其他几个动画有问题；上勾拳/裂地猛击缺失，火箭拳没有蓄力"。
根因：PR #129 的 `ow_gauntlet.bin` 是**单帧刚体姿态**（`0 clips`），`OwAnimator` 只对整只手做平移/滚转；
导出的 `punch/shoot/uppercut/slam` 骨骼动画从未进入 C++。

本轮改为**烘焙原版骨骼动画，由 CPU 蒙皮播放**，渲染复用 v1 已验证的着色器与管线：

| 层 | 改造 |
|---|---|
| 资产 | `tools/ow/build_ow_skinned.py` 读取 `doomfist_1p.json` + 5 段 `*_anim.json`，输出 **CSVM v4**：12409 顶点（section 0）、36 根手/指骨骼、5 个 named-track clip（idle/shoot/punch/uppercut/slam），每帧存 bind→viewmodel 空间 palette |
| 渲染 | 保留 v1 的 3 元素顶点布局与着色器；每帧在 CPU 用 palette 做四骨蒙皮，写入 4 槽环形上传顶点缓冲（避免 GPU 读到正在改写的槽位），世界变换仍是原来的 `toHost(base * scale)` |
| 播放 | `OwAnimator` 按 state 选 clip，keyframe 之间逐元素插值（keyframe 位精确），状态切换 80 ms 交叉淡化；idle 循环 |
| 动作 | 状态表：0 idle、2 手炮、3 火箭拳、4 上勾拳、5 大招；**E = 上勾拳，Q = 大招**（原版键位是 Shift=上勾拳、E=裂地猛击、Q=大招；本适配器保留 E=上勾拳） |
| 火箭拳输入 | 原版控制器 `右键_开始蓄力` / `右键_释放直拳`：**按住右键蓄力**，windup 停在释放帧；**松开右键出拳**（跳到 f68、播放释放音效、开始冲刺）。快速点击 = 立即出拳 |
| 射击幅度 | 原版 shoot clip 手腕前移 0.35 m、上抬 0.34 m，第一人称太飘；`ow_shoot_motion`（0–1，默认 0.4）把每根骨骼向 idle 姿态混合 |
| 时序 | 各状态时长取资产 clip 的 `(frames-1)/fps`；callback 未就绪时用烘焙常量兜底 |
| 火箭拳 | 播放完整 109 帧（3.567 s）：f0–67 蓄力，f68 释放（2.25 s），f92 完全伸展（3.05 s）。`ow.punch.release` 音效与玩家冲刺都对齐 2.25 s，不再在右键瞬间触发 |

> 为什么不用 GPU 蒙皮：第一版（palette CBV + HLSL 蒙皮）实机出现闪屏、手势/材质异常。
> CPU 蒙皮每帧约 12k 顶点 × 4 骨骼，开销可忽略，且直接沿用 v1 已验证的绘制路径。

资产与部署：

```powershell
uv run --with numpy python tools/ow/build_ow_skinned.py `
  --mesh "C:\Users\30435\ow_export\out\doomfist_1p.json" `
  --anim-dir "C:\Users\30435\ow_export\out" `
  --out "C:\Users\30435\ow_export\out\ow_gauntlet_skinned.bin"

# 部署（游戏关闭时）
Copy-Item "...\ow_gauntlet_skinned.bin" "C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mods\mc_adapter\ow_gauntlet.bin" -Force
```

验证：`uv run --with pytest --with numpy python -m pytest tests/test_ow_skinned_export.py`；
C++ 侧 `MC_OW_ASSET=<bin> mc_tests --gtest_filter=OwViewmodelTest.ParsesTheShippedAssetWhenProvided`。
实机日志应出现 `gauntlet loaded ... 36 bones, 5 clips`。

**已备份**：`Game/dinput8.dll.bak_ow_anim_20261101`、`mods/mc_adapter/ow_gauntlet.bin.bak_v1_20261101`。

### 0.4 原版 Montage / 控制器解析结论（2026-11-01）

用 CUE4Parse 直接解析原版资产（`C:\Users\30435\ow_export\Program.cs --dump/--imports`）后确认：

| 资产 | 结论 |
|---|---|
| `AM_原版直拳/手炮/上勾拳/裂地猛击` | 都是**单 section（Default）**，section 从 0 到全长，`AnimStart=0 / AnimEnd=全长 / PlayRate=1 / Looping=1`。Montage 本身没有截取子段 |
| `BP_铁拳控制器` | 函数：`右键_开始蓄力`、`右键_释放直拳`、`左键_手炮`；调用 `Montage_Play`、`Montage_GetPosition`、`Montage_SetPosition`、`WasInputKeyJustReleased`；变量 `直拳蓄力`、`直拳冲刺`、`直拳起点`、`直拳上一帧位置` |
| 直拳音效 | `MIX_原版直拳蓄力一`（蓄力）、`MIX_原版直拳释放`（释放）、`MIX_原版直拳二次撞击`（命中） |
| 裂地猛击 | `帧_技能阶段`、`帧_裂地落点`、`计算当前裂地方向`、`BP_裂地冲击`、`SFX_原版裂地飞行`，以及 `T_PBZ_SlamFade_00..16` 17 张淡出贴图 |

因此原版右键流程是 **按下开始蓄力 → 松开释放直拳**（不是松开取消），本轮已按此重做。
裂地猛击的“隐身放大”来自技能阶段切换 + SlamFade 淡出/飞行效果，需要后续按 `帧_技能阶段` 的状态机再还原。

> CUE4Parse 需要脚本映射才能把 `UFunction` 的 `ScriptBytecode` 解出来；当前只映射了 `Function`/`BlueprintGeneratedClass`/`AnimMontage` 的类哈希，字节码仍为空，交互流程是从函数名、变量名和 Montage 数据推断的。

### 0.5 大招（Q）现状（2026-11-01）

原版 Q 是 **Meteor Strike 大招**，不是裂地猛击：

- 函数：`Q_起跳选点` → `Q_确认落点`；阶段变量 `技能阶段`（待机 / 大招起跳 / 大招选点 / 大招俯视 / 大招下落 / 大招收招）
- 数据：`大招起点/落点/地面法线/前视角/动态圈`、`落点指示`、`大招选点开始`、`大招下落`、`大招俯视`、`大招收招`
- 动画：`AS_原版三人大招起飞`（15 帧 @32.1fps ≈ 0.467 s）与 `AS_原版全身大招落地`（20 帧 @31.6fps ≈ 0.633 s），**390 根骨骼的全身 3P 动画**，不是 1P 拳套骨骼
- 音效：`MIX_原版大招起手 / 预警 / 落地`；特效：`SFX_原版裂地飞行`、`T_PBZ_SlamFade_00..16`

当前实现（状态 5）先做不需要驱动游戏主相机的部分：

| 时间 | 表现 |
|---|---|
| 0.00–0.15 s | 拳套可见，开始蓄力 |
| 0.15–0.35 s | 拳套淡出（“隐身”） |
| 0.35–1.42 s | 完全隐藏（跳跃/俯视阶段） |
| 1.42–1.56 s | 淡入 |
| 1.50 s 附近 | 1.35× 缩放脉冲（“放大”冲击） |

**仍缺**：真正的俯视视角、落点选择圈、下落轨迹——这些需要驱动游戏主相机并绘制落点 UI，属于下一步。


---

## 1. 核心缺陷根因与修复细节 (Root Cause Analysis & Fixes)

### 1.1 贴图丢失 Bug（原版 OW 拳套金属与高光生效）
- **根本原因**：
  在 `adapters/eldenring/src/overlay_d3d12.cpp` 的 `loadTexturesFromFiles` 调用中：
  ```cpp
  // 错误调用：
  g_ow.loadTexturesFromFiles(g_s.device, firstExisting(albedo), "", g_ow_table_cpu, g_ow_table_cpu, ...);
  ```
  参数 4 为 `knife_slot`，参数 5 为 `glove_slot`。当把两个槽位都传成 `g_ow_table_cpu` 时：
  在 `CsRenderer::loadTexturesFromFiles` 内部，`slot0` 为 `g_ow_table_cpu`（成功上传 2048×2048 的金色拳套贴图）；但紧接着当 `glove_path` 为空时，函数向 `slot1`（因为 `glove_slot == slot0`，所以 `slot1 == slot0`！）写入了 16×16 纯灰色回退纹理 `(45, 45, 45, 255)`，**瞬间覆盖了刚写入的拳套贴图**！
- **修复措施**：
  ```cpp
  const UINT srv_inc = g_s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE glove_cpu = g_ow_table_cpu;
  glove_cpu.ptr += srv_inc;
  g_ow.loadTexturesFromFiles(g_s.device, firstExisting(albedo), "", g_ow_table_cpu, glove_cpu,
                             firstExisting(normal), "", firstExisting(pbr), "",
                             "", "", "", "", "", "");
  ```
  `slot0` 稳定绑定 `ow_gauntlet.png`，`slot2` 绑定法线 `ow_gauntlet_normal.png`，`slot4` 绑定真 PBR ORM 贴图 `ow_gauntlet_pbr.png`。

### 1.2 原版法环动作屏蔽与角色身体隐藏
- **根本原因**：
  法环原生角色控制器响应鼠标点击执行轻击 combo、强攻击与战技，均依赖 DirectInput8 的设备状态轮询。在 `mc-core` 中，`erin::GetStateDetour` 依靠 `g_suppress` 原子标志将 `buttons[0]` 和 `buttons[1]` 物理清零。
  但在 `adapters/eldenring/src/loader.cpp`：
  ```cpp
  bool WantSuppress() {
  #if ENABLE_CS2_VIEWMODEL
      if (!g_mc_mode.load() && !eldenring::RuntimeContext::Instance().mode().cs_mode.load()) return false;
  ```
  此前缺少对 `ow_mode` 的判定，导致 OW 模式下 `WantSuppress()` 始终为 `false`，鼠标按键从未被压制，点击时原生法环角色依然挥舞武器。
- **修复措施**：
  1. `WantSuppress()` 增加 `!mode.ow_mode.load()` 判定，激活 DInput8 鼠标清零逻辑。
  2. 控制策略 `buildControlPlan(control_inputs)` 中，`in.ow_mode` 请求 `mc::requestMinecraftCombat`，使 `control_plan.blocksNative(ControlDomain::MouseButtons)` 与 `ControlDomain::Attack` 为 `true`。
  3. `loader.cpp` 的 `UpdateNativeModel` 统一在 `(out.mc_mode || out.cs_mode || ow_fp)` 时隐藏法环角色的原生模型与持握武器，杜绝穿模与动作干扰。

### 1.3 攻击动作接入与状态机
> **2026-11-01 更新**：本节描述的程序化整体变换已被 §0.3 的原版骨骼动画取代，保留为历史记录。
- **状态机与时序控制**：
  - 在 `ModeState` 新增 `std::atomic<uint8_t> ow_state{0};`、`std::atomic<float> ow_anim_time{0.f};`、`std::atomic<uint8_t> ow_action_variant{0};`。
  - **左键（Primary Attack / 状态 2）**：触发快速手炮直拳，前冲位移 $-0.16\text{m}$，伴随 $10^\circ$ 微滚转倾角，总时长 $0.40\text{s}$。
  - **右键（Secondary Attack / 状态 3）**：触发火箭蓄力重拳 / 上勾拳，前 $0.20\text{s}$ 蓄力后拉拉满 $+0.04\text{m}$，随后爆发冲拳深度前推 $-0.28\text{m}$ 并伴随 $+0.07\text{m}$ 上挑俯仰，总时长 $0.80\text{s}$。
  - 并在 `hotkey_dispatcher.cpp` 中以每帧 $dt$ 递增计时器，超时后无缝复位为待机状态 0。

---

## 2. 深度架构解耦分析（针对“目前还是没和其他模式区分开来”）

> **状态：已完成（2026-10-31）。** 以下清单保留为设计依据；实际落地见 §0.1 与 §5。

用户敏锐地指出：当前 OW 模式虽然功能跑通，但**底层架构上尚未彻底独立**。以下为详细解耦清单：

```mermaid
flowchart TD
    subgraph 当前状态 (Coupled)
        CS_MACRO["ENABLE_CS2_VIEWMODEL 宏"] --> CS_RENDERER["CsRenderer 通用类"]
        CS_RENDERER --> G_OW["g_ow 实例 (overlay_d3d12.cpp)"]
        HEAP["CS2 SRV 堆 (Slots 48-63)"] --> G_OW
        CONFIG["mc_er_steve.txt 混合配置"] --> OW_FLAGS["ow_mode 等状态位"]
    end

    subgraph 目标独立状态 (Fully Decoupled)
        OW_MACRO["ENABLE_OW_VIEWMODEL 独立构建宏"] --> OW_RENDERER["OwRenderer 专属视口类"]
        OW_RENDERER --> OW_HEAP["独立 OW SRV Heap / 专用 RootSignature"]
        OW_CONFIG["mc_er_ow.txt 专属配置文件"] --> OW_STATE["专属 OwModeContext / State"]
```

### 耦合点清单与改造步骤：
1. **构建宏解耦**：
   - 当前：`CMakeLists.txt` 中仅定义了 `ENABLE_CS2_VIEWMODEL`，没有它就无法编译 `g_ow`。
   - 目标：在根 `CMakeLists.txt` 增加 `option(ENABLE_OW_VIEWMODEL "Build Overwatch Doomfist First-Person Viewmodel" ON)`，与 `ENABLE_CS2_VIEWMODEL` 完全正交，彼此可单独开启或关闭。
2. **渲染器类解耦**：
   - 当前：`overlay_d3d12.cpp` 中定义 `CsRenderer g_ow;`，并借用 `CsRenderer` 的 shader 与管线。
   - 目标：将底层通用的第一人称 D3D12 光栅化管线抽象为 `fp::ViewmodelRendererBase`，派生出 `CsRenderer` 与 `OwRenderer`；或者直接提取 `adapters/eldenring/src/ow_renderer_d3d12.cpp`，消除对 CS 命名空间的任何调用。
3. **描述符堆与着色器解耦**：
   - 当前：OW 挤在 CS 堆的 slots 48–63。
   - 目标：为 OW 分配专用的 `g_ow_srv_heap` 或完全独立的根参数，避免共享槽位引起的潜在溢出。
4. **配置文件与热键解耦**：
   - 当前：配置项混合在 `mc_er_steve.txt`。
   - 目标：建立 `mc_er_ow.txt`，支持纯粹的 OW 自定义参数（如拳套缩放比例 `ow_scale`、FOV 视距 `ow_fov`、出拳速度系数等）。
5. **音效映射解耦**：
   - 当前：出拳使用 `cs.knife.slash` 与 `cs.knife.hit` 作为回退音效。
   - 目标：从已解压的 `ProjectPBZ/Content/Mods/WuchangDoomfist/Original/Audio/` 提取原版 `MIX_原版手炮完整开火.ubulk` 和 `MIX_原版直拳释放.ubulk`，转为 WAV 并注册为 `ow.cannon.fire` 和 `ow.punch.release` 原版音效。

---

## 3. 完整资产与工程文件映射 (Asset & Tooling Map)

| 资产 / 工具文件 | 路径 | 说明 |
|---|---|---|
| **原始 Mod 包** | `D:\code\universal-modder\影之刃零_第一人称末日铁拳MOD` | 包含 `1_100_P.utoc/.ucas` |
| **已解包原始资产** | `D:\code\universal-modder\ow_assets\pbz_unpack` | 包含 143 个原生 `.uasset/.ubulk` |
| **反解动画 JSON** | `C:\Users\30435\ow_export\out\idle_anim.json` | 待机序列（40 FPS，4 帧循环） |
| **攻击动画 JSON** | `C:\Users\30435\ow_export\out\punch_anim.json` | 原版直拳攻击（30.28 FPS，109 帧） |
| **手炮动画 JSON** | `C:\Users\30435\ow_export\out\shoot_anim.json` | 原版手炮射击（31.07 FPS，29 帧） |
| **上勾拳动画 JSON** | `C:\Users\30435\ow_export\out\uppercut_anim.json` | 原版上勾拳动作（27 帧） |
| **裂地猛击动画 JSON** | `C:\Users\30435\ow_export\out\slam_anim.json` | 原版裂地猛击动作（61 帧） |
| **骨骼动画烘焙** | `tools/ow/build_ow_skinned.py` | 5 段 clip + 36 骨骼 → CSVM v4（GPU 蒙皮） |
| **已部署新资产** | `mods/mc_adapter/ow_gauntlet.bin` | v4 skinned，约 1.2 MB，5 clips |
| **网格模型 JSON** | `C:\Users\30435\ow_export\out\doomfist_1p.json` | 21,356 顶点，126 骨骼，83,592 索引 |
| **反解工具 (C#)** | `C:\Users\30435\ow_export\Program.cs` | 包含 IoPackage 导入修复与 Mock Codec 注入 |
| **LBS 蒙皮工具** | `C:\Users\30435\ow_export\export_posed_csvm.py` | 依据 FK/LBS 及 Camera 逆矩阵输出二进制 CSVM |
| **一键部署脚本** | `C:\Users\30435\ow_export\deploy_ow.py` | 编译 CSVM、导出预览并物理部署 DLL/贴图至游戏目录 |
| **原版音效提取** | `tools/extract_ow_sounds.py` | 解析 `.ubulk`（SEEK 表 + Bink Audio 2 块）→ UEBA `.binka` → FFmpeg → 44.1 kHz WAV，并更新 `sounds_manifest.txt` |
| **游戏物理部署目录** | `C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\` | 包含 `dinput8.dll` 及 `mods/mc_adapter/` 全量文件 |

---

## 4. 可复现执行手册 (Reproduction & Verification Commands)

### 4.1 运行 C# 动画/网格/贴图反解器
```powershell
cd C:\Users\30435\ow_export
powershell -ExecutionPolicy Bypass -File build.ps1
dotnet bin\ow_export.dll
```
*预期输出*：成功导出 `doomfist_1p.json`、`gauntlet_albedo.png`、`idle_anim.json`、`punch_anim.json`、`shoot_anim.json` 等全部数据。

### 4.2 导出姿势并一键部署游戏存档
```powershell
python C:\Users\30435\ow_export\deploy_ow.py
```
*预期输出*：
```text
DEPLOYED: dinput8.dll            (2,353,664 bytes)
DEPLOYED: ow_gauntlet.bin        (1,017,810 bytes)
DEPLOYED: ow_gauntlet.png        (2,798,336 bytes)
DEPLOYED: ow_gauntlet_normal.png (982,045 bytes)
DEPLOYED: ow_gauntlet_pbr.png    (595,397 bytes)
```

### 4.3 构建与全量自动化测试
```powershell
cd D:\code\universal-modder\mc-core
cmake --build build --config RelWithDebInfo --target eldenring_adapter mc_tests
ctest --test-dir build -C RelWithDebInfo --output-on-failure -R "(OwDoomfist|EldenRingControlPolicy)"
```
*测试通过断言*：17/17 项测试 100% Passed。

---

## 5. 接力路线图 (Next Steps Roadmap)

- [x] **Step 1: CMake 宏独立化**：新增 `ENABLE_OW_VIEWMODEL`，与 `ENABLE_CS2_VIEWMODEL` 完全正交。
- [x] **Step 2: 提取独立 OwRenderer**：`erow::OwRenderer` 拥有自己的根签名、PSO、深度缓冲、VB/IB 与 3 槽 SRV 堆；不再调用任何 `eldenring::cs` 类型。
- [x] **Step 3: 专属配置文件**：`mc_er_ow.txt` 支持 `ow_mode` / `ow_fov` / `ow_scale` / `ow_offset_x|y|z`，即使 `mc_er_steve.txt` 缺失也会加载。
- [x] **Step 4: 原版音效提取**：`tools/extract_ow_sounds.py` 解析 `.ubulk` 的 `SEEK` 表与 `0x9999` Bink Audio 2 块，重建 UEBA `.binka` 容器交给 FFmpeg 解码为 16-bit 44.1 kHz WAV，并注册 `ow.cannon.fire` / `ow.punch.release`。

### 5.0 Step 4 使用说明

```powershell
# 需要 2025-09 之后、带 UEBA binka 支持的 FFmpeg
uv run python tools/extract_ow_sounds.py `
  --audio-dir "D:\code\universal-modder\ow_assets\pbz_unpack\Mods\WuchangDoomfist\Original\Audio" `
  --out-dir "C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mods\mc_adapter" `
  --ffmpeg "<path-to-ffmpeg.exe>"
```

- 输出：`sounds/ow/ow_cannon_fire.wav`（4.275 s）、`sounds/ow/ow_punch_release.wav`（1.479 s）
- 清单：在 `sounds_manifest.txt` 中幂等追加 `ow.*` 行，不触碰其他事件
- 解码要点：`.ubulk` 块头为 `0x9999 | (帧长 << 16)`；最后一帧是 trimmed 帧（`0xffff` + 帧长 + 有效采样数），必须保留该头，FFmpeg 才会丢弃 overlap padding
- UE 变体必须走 **DCT** 解码器；强制 `binkaudio_rdft` 会得到满幅噪声

### 5.1 本轮验证命令与结果

```powershell
cd D:\code\universal-modder\mc-core

# 1) CS2 + OW 双开
cmake -S . -B build -DENABLE_CS2_VIEWMODEL=ON -DENABLE_OW_VIEWMODEL=ON
cmake --build build --config RelWithDebInfo --target eldenring_adapter mc_tests
ctest --test-dir build -C RelWithDebInfo --output-on-failure
# -> 1245/1245 passed (2 项按设计 Skip)

# 2) 仅 OW（关键解耦证明）
cmake -S . -B build_ow_only -DENABLE_CS2_VIEWMODEL=OFF -DENABLE_OW_VIEWMODEL=ON
cmake --build build_ow_only --config RelWithDebInfo --target eldenring_adapter mc_tests
# -> eldenring_adapter / mc_tests 编译通过；OwViewmodelTest 6/6 passed

# 3) 仅 CS2
cmake -S . -B build_cs_only -DENABLE_CS2_VIEWMODEL=ON -DENABLE_OW_VIEWMODEL=OFF
cmake --build build_cs_only --config RelWithDebInfo --target eldenring_adapter mc_tests
ctest --test-dir build_cs_only -C RelWithDebInfo --output-on-failure
# -> 1238/1238 passed

# 4) 纯 MC
cmake -S . -B build_mc_only -DENABLE_CS2_VIEWMODEL=OFF -DENABLE_OW_VIEWMODEL=OFF
cmake --build build_mc_only --config RelWithDebInfo --target eldenring_adapter mc_tests
# -> 编译通过
```

### 5.2 `mc_er_ow.txt` 示例

```ini
ow_mode=1
ow_fov=68
ow_scale=1.0
ow_offset_x=0
ow_offset_y=0
ow_offset_z=0
ow_dash_distance=4.0
ow_shoot_motion=0.4
```

### 5.3 仍需实机确认

- [ ] 用新的 `build\bin\eldenring\dinput8.dll` 启动游戏，Shift+F7 进入 OW，确认拳套贴图、PBR 高光与左/右键动作与改造前一致。
- [ ] 在 `mc_er_ow.txt` 中调整 `ow_fov` / `ow_scale`，确认实时生效。
- [ ] 确认切出 OW（F6）后 CS / MC 模式行为无回归。

### 5.4 右键火箭冲拳位移（2026-10-31）

- **纯逻辑**：`include/eldenring_ow_dash.hpp`
  - `rocketPunchSpeed(t, cfg)`：`sin(pi * p)` 速度曲线，窗口 `[2.25, 3.05]`（对齐原版直拳 f68–f92），总位移严格等于 `distance_m`
  - `rocketPunchDistance(t, cfg)`：积分后的累计位移，便于测试与调试
  - `rocketPunchDirection(forward, x, z)`：把相机前向投影到水平面；俯仰接近垂直时返回 false
- **执行层**：`PlayerLocomotion::stepOwRocketPunch(dt, ctx)`，由 `on_camera_step_pre` 每帧调用
  - 只在 `ow_mode && ow_state==3`、前台、非背包、非死亡时运行
  - 沿相机水平前向写 `kPhysicsPosition` 与 `+0x10`，并置 `kPhysicsProxySyncRequest`
  - 用 `CastStaticRay` 在 `feet+0.4` 与 `feet+1.0` 两个高度向前投射，按命中比例夹紧步长，保留 0.4 m 胶囊半径
  - 同时把角色朝向转到冲拳方向
- **配置**：`mc_er_ow.txt` 的 `ow_dash_distance`（米，默认 4.0，范围 0–15）
- **测试**：`tests/test_ow_dash.cpp`（窗口外为 0、峰值、数值积分等于总位移、单调性、水平投影）

**注意**：这是位置写入而非速度写入。原生游戏的角色控制器每帧也会写位置；冲刺增强（`stepGroundSprint`）证明了同路径可行，但冲拳步长更大，实机需要重点看是否抖动、是否被原生动画/行为拉回。若出现回拉，下一步改为写 `PhysicsModule+0x120` 线速度并让原生物理积分。

---

## 6. 下一步：大招（Q）逆向与实现交接（2026-11-01）

### 6.0 当前状态（接手先看）

- **已推送**：PR #129，分支 `feat/ow-only`，头 `c966604`（本文件之后的 commit 会继续更新）。
- **已完成**：1P 拳套 CSVM v4（36 骨骼、5 clip）+ CPU 蒙皮；LMB 手炮、RMB 按住蓄力/松开直拳、E 上勾拳、Q 大招淡出/放大近似；`ow_shoot_motion` 调射击幅度。
- **已部署**：`Game\dinput8.dll`、`mods\mc_adapter\ow_gauntlet.bin`（v4，约 1.2 MB）；备份 `dinput8.dll.bak_ow_anim_20261101`、`ow_gauntlet.bin.bak_v1_20261101`。
- **测试**：`mc_tests` 1262 passed（`ParsesTheShippedAssetWhenProvided` 需设置 `MC_OW_ASSET` 才运行）。
- **Q 现状**：状态 5 播放 slam clip，0.15–0.35 s 淡出、0.35–1.42 s 隐藏、1.42–1.56 s 淡入、1.50 s 1.35× 缩放脉冲。渲染器已开启 alpha 混合，`tint.a` 控制透明度。**这不是原版大招**，只是“隐身放大”的视觉近似。

### 6.1 原版事实（已解析，不要再猜）

- **键位**：LMB `左键_手炮`；RMB `右键_开始蓄力` → `右键_释放直拳`；Shift `Shift_上勾拳`；E `E_裂地猛击`；Q `Q_起跳选点` → `Q_确认落点`（大招）。
- **Montage**：`AM_原版直拳/手炮/上勾拳/裂地猛击` 都是单 section（Default），`AnimStart=0 / AnimEnd=全长 / PlayRate=1 / Looping=1`；直拳 3.6 s、手炮 0.933 s、上勾拳 0.867 s、裂地 2.0 s。
- **控制器变量**：`技能阶段`（待机 / 大招起跳 / 大招选点 / 大招俯视 / 大招下落 / 大招收招）、`大招起点/落点/地面法线/前视角/动态圈`、`大招选点开始`、`落点指示`、`裂地落点/法线/方向/飞行终点`、`裂地本次伤害=90`、`裂地轻抬速度=313.04953`、`直拳起点/上一帧位置/冲刺/蓄力`、`下次直拳/下次上勾/下次大招/下次裂地`。
- **大招动画**：`AS_原版三人大招起飞`（15 帧 @32.143 fps = 0.467 s）、`AS_原版全身大招落地`（20 帧 @31.579 fps = 0.633 s），**390 根骨骼全身 3P**；`Camera` 轨道：起飞 z -4.906..-3.492、y -2.892..-2.805；落地 y -3.223..-1.988。
- **音效/特效**：`MIX_原版大招起手/预警/落地`、`MIX_原版直拳蓄力一/释放/二次撞击`；`SFX_原版裂地飞行`、`BP_裂地冲击`、`SM_裂地梯形`、`T_PBZ_SlamFade_00..16`。
- **原版 Q 是 Meteor Strike**：起跳 → 俯视选点（动态圈/落点指示）→ 确认 → 下落 → 落地 → 收招。

### 6.2 逆向清单（按优先级）

**P0：控制器字节码（最大卡点）**

| 目标 | 要拿到 |
|---|---|
| `Q_起跳选点` / `Q_确认落点` | 阶段切换条件、相机参数、落点选择算法、取消条件 |
| `帧_技能阶段` | 阶段枚举值与每阶段时长 |
| `帧_鼠标` / `帧_移动` / `帧_墙撞` | 俯视鼠标控制、起跳/下落速度、碰撞夹紧 |
| `公共_取消技能` | 取消条件与恢复流程 |

**P1：相机与落点**

- 游戏主相机位置/俯仰/FOV 的可写偏移；`大招前视角` 的保存/恢复。
- `Q_确认落点` 的地面射线：屏幕点 → 世界射线 → 地面。
- `大招动态圈` / `落点指示`：网格/材质、缩放规则、显示时机。
- `帧_俯视` 的相机曲线（可先用 `ult_takeoff.json` 的 Camera 轨道反推）。

**P2：轨迹与落地**

- `帧_移动`、`大招下落`、`裂地飞行终点`、`裂地轻抬速度` 的速度/终点算法。
- `大招落点/地面法线` 的贴地与斜坡处理。
- `BP_裂地冲击`、`SM_裂地梯形`、`MI_PBZ_裂地梯形` 的生成时机与参数。

**P3：音效与淡出**

- `MIX_原版大招起手/预警/落地` 解码为 WAV、注册事件、绑定阶段。
- `SFX_原版裂地飞行` 是否需要在飞行阶段播放。
- `T_PBZ_SlamFade_00..16` 的用途（屏幕淡出/角色淡出/落点圈动画）与材质参数。

**P4/P5（可选）**：右键直拳的 `Montage_SetPosition` 落点与蓄力→冲刺映射；原版 E 裂地猛击（当前 E 是上勾拳）。

### 6.3 卡点与解锁方式

`BP_铁拳控制器` 的 UFunction `ScriptBytecode` 目前为 `null`，因为 CUE4Parse 缺少脚本映射（日志里的 `Missing script import`）。

已映射（`Program.cs::InjectEmptyGlobalData`）：

- `0x174F27AEC05072D0` → `Function`
- `0x2B62D0815847A520` → `BlueprintGeneratedClass`
- `0x11361FDBC40AEECC` / `0xFE00966596DA663` → `AnimMontage`
- ABP 相关待映射：`0x1E57B5282C965C3F`（推测 `AnimBlueprintGeneratedClass`）、`0x2C00F707978C8EF5`（推测 `AnimInstance`）

解锁优先级：

1. **最优**：拿到 Phantom Blade Zero 的 `Content/Paks/global.utoc/.ucas`（官方脚本映射），用 CUE4Parse 正常加载后重新 `--dump`，字节码即可解出。
2. **次优**：安装 PBZ + 原 mod，录屏观察真实右键/大招表现（相机高度/俯仰、阶段时长、落点圈），据此实现，不必解字节码。
3. **兜底**：手动定位 UFunction 导出字节码偏移，用 `KismetExpression.ReadExpression` 解码（工作量大）。

> 机器上目前没有找到 PBZ 安装；只有 Epic 启动器图标和 mod 压缩包 `D:\game\mc-core\影之刃零_第一人称末日铁拳MOD.rar`。

### 6.4 工具与产物

`C:\Users\30435\ow_export\Program.cs` 新增模式：

| 命令 | 用途 |
|---|---|
| `--dump <needle>` | 导出包内所有 export、属性、Montage section/segment、UFunction flags/bytecode、NameMap |
| `--imports <needle>` | 打印 ImportMap/ExportMap（ClassIndex/Super/Template/Outer） |
| `--anim <needle> <out.json>` | 导出任意动画序列 |
| `--types <needle>` | 反射 CUE4Parse 类型成员 |
| `--findhash <hex...>` | 候选脚本名哈希匹配（当前未匹配到缺失哈希） |

构建：`powershell -ExecutionPolicy Bypass -File build.ps1`，然后 `dotnet bin\ow_export.dll ...`。

产物：

- `out\dump_bp_controller.txt`（控制器全部导出、变量、NameMap）
- `out\imports_bp.txt`（导入/导出映射）
- `out\dump_AM_原版*.txt`（四个 Montage 的 section/segment）
- `out\ult_takeoff.json` / `out\ult_landing.json`（大招全身动画，含 Camera 轨道）

**烘焙工具未提交**：`D:\code\universal-modder\tools\ow\build_ow_skinned.py` 和 `tests\test_ow_skinned_export.py` 在 universal-modder 工作区里，属于 toolkit 仓库；下次接手先提交。

### 6.5 建议实现方案（大招）

- **阶段**：起跳（0.47 s）→ 俯视选点（可配置 0.8–1.5 s）→ 下落（约 0.4 s）→ 落地/收招（0.63 s），总时长约 2.2–3.0 s。
- **相机**：复用现有相机 hook，起飞时抬升 + 俯视，确认后下落；保存/恢复 `大招前视角`。
- **选点**：屏幕中心 → `CastStaticRay` 取地面点；HUD 画圆（参考 `SM_范围平面` / `MI_PBZ_毁天灭地范围`）。
- **1P 表现**：飞行阶段保持拳套淡出；落地用缩放脉冲 + 冲击特效；接 `MIX_原版大招起手/预警/落地`。
- **不依赖字节码可先做**：音效提取/注册、SlamFade 贴图用途确认、落地冲击特效、淡出/放大参数微调。
- **依赖字节码/录屏才能定**：阶段时长、相机曲线、选点规则、取消条件、伤害/击飞。

### 6.6 下次开工第一件事

1. 问用户是否有 PBZ 安装；有则拿 `global.utoc/.ucas`（最优），或录原 mod 视频。
2. 没有则手动解码 UFunction 字节码（从 `右键_释放直拳` 和 `帧_技能阶段` 开始，体量最小、信息量最大）。
3. 更新本节，把拿到的阶段表和相机参数写进来。

