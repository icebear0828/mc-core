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
```

### 5.3 仍需实机确认

- [ ] 用新的 `build\bin\eldenring\dinput8.dll` 启动游戏，Shift+F7 进入 OW，确认拳套贴图、PBR 高光与左/右键动作与改造前一致。
- [ ] 在 `mc_er_ow.txt` 中调整 `ow_fov` / `ow_scale`，确认实时生效。
- [ ] 确认切出 OW（F6）后 CS / MC 模式行为无回归。

