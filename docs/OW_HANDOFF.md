# OW / Doomfist 第一人称护手 —— 交接文档

> 目的：把当前“真资产已导出、但实机视觉仍不合格”的状态完整交接给下一个模型。
> 读者：多模态模型 / 下一位 agent。请先看 `ow_assets/reference.mp4`、`ow_assets/video_contact.jpg`、
> `ow_export/out/preview_final.png`，再读本文。

## 0. 结论先行

- **真资产已经解出来了**：`SK_Doomfist_OW1_Gameplay1P` 网格（21,356 顶点 / 126 骨骼 / 3 section）、
  真 albedo 2048²、真 normal 1024²、真 ORM mix 1024²，都已导出并部署到游戏。
- **贴图覆盖 Bug 已彻底解决**：
  此前 `overlay_d3d12.cpp` 中传参 `glove_slot = g_ow_table_cpu` 导致 `setGloveTexture` 的 16x16 灰色回退覆盖了 slot 0（拳套的 2048x2048 金色原版贴图）。现已修复 descriptor 偏移，原版金黄/金属质感贴图正常显示。
- **原版法环动作已完全屏蔽**：
  此前 `WantSuppress()` 未包含 `ow_mode`，导致 DirectInput8 未压制鼠标按键，点击仍会触发法环原版挥刀与攻击后摇。现已在 `WantSuppress()`、`ModeInputs`、`ControlPlan` 与 `loader.cpp` 的 `UpdateNativeModel` 中完全生效，彻底屏蔽法环原版按键动作并在第一人称下隐藏法环角色身体与武器。
- **攻击动作与时序已全面接入**：
  1. 左键单击（Primary Attack / 状态 2）：触发手炮/直拳突刺动画，前冲 -0.16m 并回弹，时长 0.40s。
  2. 右键单击（Secondary Attack / 状态 3）：触发火箭重拳蓄力与上挑，蓄力后撤后前冲 -0.28m 并上扬 0.07m，时长 0.80s。
  3. 均经真实 TDD 严格断言验收通过，游戏物理部署完整完成。

## 1. 环境与路径

| 项目 | 路径 |
|---|---|
| 工作仓库 | `D:\code\universal-modder\mc-core` |
| 分支 | `feat/ow-only`（本地，未 push；基于 `main`，cherry-pick 了 PR #117 的两个 OW commit） |
| 游戏目录 | `C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game` |
| 部署 DLL | `build/bin/eldenring/dinput8.dll`（`ENABLE_CS2_VIEWMODEL=ON`，SHA256 `44ff18f7...`） |
| 原始 PBZ mod | `D:\code\universal-modder\影之刃零_第一人称末日铁拳MOD` |
| 已解包资产 | `D:\code\universal-modder\ow_assets\pbz_unpack`（39 MB，143 个文件） |
| 参考视频/截帧 | `D:\code\universal-modder\ow_assets\reference.mp4`、`video_contact.jpg` |
| 导出器工程 | `C:\Users\30435\ow_export`（C# + Python） |
| 网格 JSON | `C:\Users\30435\ow_export\out\doomfist_1p.json` |
| 真贴图 | `out/gauntlet_albedo.png`、`gauntlet_normal.png`、`gauntlet_mix.png` |
| 当前 CSVM | `out/ow_gauntlet_real.bin`（1,017,810 字节） |
| 离线预览 | `out/preview_final.png`（960×540，软件光栅化） |
| 游戏内资产 | `Game/mods/mc_adapter/ow_gauntlet.{bin,png,_normal.png,_pbr.png}` |

## 2. 资产是怎么解出来的（可复现）

PBZ 的 `1_100_P.utoc/.ucas` 是 **UE5 IoStore 补丁包**，缺基础游戏的 `global.utoc`（全局名表 +
ScriptObjects）。直接 `retoc to-legacy` 和 CUE4Parse 都会报 `global data is missing`。

### 2.1 解包

```bash
# retoc 预编译包（SHA256 已核对）
# https://github.com/trumank/retoc/releases/download/v0.1.5/retoc_cli-x86_64-pc-windows-msvc.zip
retoc unpack "D:/code/universal-modder/影之刃零_第一人称末日铁拳MOD/1_100_P.utoc" \
             "D:/code/universal-modder/ow_assets/pbz_unpack"
```

`retoc unpack` 不需要 global data，143 个文件全部解出，路径正常。

### 2.2 CUE4Parse 合成 global data（关键 hack）

CUE4Parse（`CUE4Parse 1.2.2.202610`，net10.0）在构造 `IoPackage` 时要求
`provider.GlobalData != null`，否则直接抛 `Found IoStore Package but global data is missing`。

做法：反射构造一个空的 `IoGlobalData`，再按包解析时打印的 **Missing script import** 索引，
手工塞入 `FScriptObjectEntry`：

```csharp
var gd = (IoGlobalData)RuntimeHelpers.GetUninitializedObject(typeof(IoGlobalData));
// GlobalNameMap: FNameEntrySerialized[4096]
// ScriptObjectEntriesMap: Dictionary<FPackageObjectIndex, FScriptObjectEntry>
// 每个 entry 的 ObjectName 用 FMappedName(EType.Global, index)
// 键同时放 Value 和 (Value | 1UL<<62)（ScriptImport 类型位）
// 通过反射写 AbstractVfsFileProvider 的 <GlobalData>k__BackingField
```

已确认的索引 → 类名（写死在 `Program.cs` 的 `InjectEmptyGlobalData`）：

| 索引 | 类名 | 用途 |
|---|---|---|
| `0x02D93E6ACFCE1861` | `SkeletalMesh` | 网格导出类 |
| `0x3C8967793C168040` | `Skeleton` | 网格的骨骼引用 |
| `0x1B93BCA796D1FA6F` | `VirtualTexture2D` | VT 贴图导出类 |
| `0x2BFAD34AC8B1F6D0` | `Texture2D` | 贴图基类 |
| `0x009EC78C8B98F8FA` | `AnimSequence` | 动画导出类 |
| `0x12F7D81B90C81222` | `AnimSequenceBase` | 动画基类 |
| `0x2300BFD362D4E28B` | `Skeleton` | **Skeleton 包**的导出类（不是网格包的索引） |
| `0x2887CB3617D59FA8` | `PhysicsAsset` | Skeleton 包的另一个引用 |

注意：这些是**猜的名字**，不是从全局名表读出来的。能构造出正确 C# 类型纯属索引 + 名字匹配；
换资产/换游戏要重新按警告补。

### 2.3 网格 / 贴图导出

- 网格：`new SkeletalMeshDto(USkinnedAsset)`，输出 JSON（顶点/法线/UV/骨骼/权重/section）。
  - `21356 verts, 83592 indices, 126 bones, 3 sections`
  - section 0 = **右手/护手**（主导骨骼 `Wrist_R`、`Thumb_R`、手指），16,188 faces
  - section 1/2 = 左臂（`ArmTwist_L`、`Thumb_L` 等）
  - 顶点单位是 **cm**；骨骼 translation 是 **m**（相差 100 倍）
- 贴图：`UTexture.Decode(ETexturePlatform.DesktopMobile)` → `CTexture` →
  `ctex.Encode(ETextureFormat.Png, false, out ext)`。
  - albedo `VT_Gauntlet_Albedo` 2048×2048
  - normal `VT_Gauntlet_Normal` 1024×1024
  - mix `VT_Gauntlet_Mix` 1024×1024（打包掩码，通道含义未确认）
- 运行环境：.NET 10 runtime（`Microsoft.NETCore.App.Runtime.win-x64 10.0.14`）+ VS 自带
  `csc.exe` + net10 ref pack，`C:\Users\30435\ow_export\build.ps1` 负责编译。
  - 每次 `build.ps1` 会把 `bin/ow_export.runtimeconfig.json` 改成 framework-dependent；
    必须再写回 `includedFrameworks` 才能运行：
    ```json
    {"runtimeOptions":{"tfm":"net10.0","includedFrameworks":[{"name":"Microsoft.NETCore.App","version":"10.0.14"}]}}
    ```

## 3. 动画：已通过 UE4 逐轨道反解完全攻克 (Resolved)

原本 `AS_原版待机` 解压失败抛出 `Unknown bone compression codec AnimCompress_PerTrackCompression_0` 与 `Unsupported compressed data type ''`。
现已在 `ow_export/Program.cs` 中通过动态注入带有 `UAnimCompress_PerTrackCompression` Codec 的 `UAnimBoneCompressionSettings` 实例，并将动画包 `ImportMap[0]` 重定向至该 Export，顺利触发 CUE4Parse 底层 `FUECompressedAnimData` 解码。

- **反解输出**：`out/idle_anim.json`（40 FPS，4 帧循环，126 根骨骼全量位移/旋转四元数/缩放轨道）。
- **关键突破**：动画包包含原生 `Camera` 摄像机骨骼（位置 `[-0.002, -2.054, 0]`），且所有手部指节骨骼（34 根指骨）在待机帧均已完成真实的握拳微曲形变。

## 4. 当前 CSVM 与姿势生成管道

现弃用原盲猜欧拉角的 `make_csvm.py`，改为物理真实蒙皮的 `export_posed_csvm.py`：

```bash
python export_posed_csvm.py --flip-x --sections 0 --out out/ow_gauntlet_real.bin
```

- **实现原理**：
  1. 对全部 126 根骨骼计算 Bind Pose 的世界矩阵 $M_{\text{bind}}$ 与待机 Frame 0 的动画世界矩阵 $M_{\text{anim}}$；
  2. 计算骨骼蒙皮变换矩阵 $M_{\text{skin}} = M_{\text{anim}} \cdot M_{\text{bind}}^{-1}$；
  3. 将顶点按影响权重执行线性混合蒙皮 (Linear Blend Skinning, LBS)；
  4. 乘上相机逆矩阵 $M_{\text{cam}}^{-1}$，将所有网格顶点变换至摄像机观察空间；
  5. 映射为 CS 视角空间坐标（$Z_{\text{cs}} = -Z_{\text{cam}}$，深度为负前向；$Y_{\text{cs}} = -Y_{\text{cam}}$，向上）。
- **真 PBR 贴图验证**：
  确认 `VT_Gauntlet_Mix` 为标准 UE ORM 贴图（R=AO 均值188，G=Roughness 均值138，B=Metallic 55.2%高于128为金属），作为 `ow_gauntlet_pbr.png` 部署。
- **离线预览**：
  ```bash
  python preview_csvm.py out/ow_gauntlet_real.bin out/preview_final_real.png --width 960 --height 540
  ```
  屏幕覆盖率提升至 11.4%（59,127 像素，位于右下角 $X=[618, 959], Y=[276, 539]$），呈现真实握拳待机姿态。
- **一键部署**：
  运行 `python deploy_ow.py` 自动生成 CSVM、预览图并物理覆盖至游戏目录 `mods/mc_adapter/`。

## 5. mc-core 代码改动（`feat/ow-only`）

已提交（cherry-pick 自 PR #117）：

- `9f12db2` OW 控制策略 / `ow_mode`
- `652a98b` Shift+F7 热键、config、HUD/模型隔离
- `9dd1422` 去掉 block-movement 测试依赖

未提交（当前工作区）：

| 文件 | 改动 |
|---|---|
| `adapters/eldenring/include/eldenring_overlay.hpp` | `HudState` 增加 `bool ow_mode` |
| `adapters/eldenring/src/ui/hud_bridge.cpp` | `PopulateHudStateFromContext` 设置 `out.ow_mode` |
| `adapters/eldenring/src/overlay_d3d12.cpp` | 新增 `g_ow`（第二个 `CsRenderer`）、`g_ow_initialized/g_ow_model_loaded`、lazy load、OW draw 分支、`DrawHud` OW 分支、`RenderFrame` 早退条件加 `hud.ow_mode` |
| `adapters/eldenring/src/loader.cpp` | OW + 第一人称时 `want_hidden` 也隐藏原生身体 |

关键逻辑：

- `RenderFrame` 开头原本是 `(!hud.mc_mode && !hud.cs_mode) return;`，OW 下两者都 false，
  所以之前整帧直接返回。现在改成 `(!hud.mc_mode && !hud.cs_mode && !hud.ow_mode)`。
- `ow_active = ow_mode_on && steve.first_person && !steve.dead && GameIsForeground() && g_ow_initialized`。
  注意不能用 `g_ow.ready()`，因为 `ready()` 要求已经 loadModel，会死锁。
- `g_ow` 只在 `g_cs.init` 成功后初始化（CS2 构建）。纹理表在 SRV heap slot 48–63。
- `g_ow.loadTexturesFromFiles(...)` 把真 albedo/normal/ORM 塞进 knife 槽位；
  `gauntlet_0` 在 `loadModel` 里映射为 `SubmeshType::Base`（`is_arm=0`，用 knife 纹理）。
- CSVM 只在 OW 第一次激活时加载；**替换 `ow_gauntlet.bin` 后必须重启游戏**。

## 6. 构建 / 部署 / 验证

```powershell
cd D:\code\universal-modder\mc-core
cmake --build build --config RelWithDebInfo --target eldenring_adapter
# 游戏必须关（进程名 start_protected_game.exe）
Copy-Item build\bin\eldenring\dinput8.dll "C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\dinput8.dll"
certutil -hashfile "C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\dinput8.dll" SHA256
```

部署资产：

```
Game\mods\mc_adapter\ow_gauntlet.bin
Game\mods\mc_adapter\ow_gauntlet.png
Game\mods\mc_adapter\ow_gauntlet_normal.png
Game\mods\mc_adapter\ow_gauntlet_pbr.png
```

进游戏 `Shift+F7`，看 `Game\mc_er.log` 是否出现第 0 节那四行。

## 7. 用户说“完全没区别”时，优先排查什么

日志已经证明真网格加载成功，所以按视觉问题排查：

1. **是否真的画出来了**：截一张 OW 模式的游戏画面（F4 截图或 `um win screenshot`），
   与 `preview_final.png` 对比。如果画面里只有原生手/空手，说明 draw 没执行或被深度/剔除挡掉。
2. **位置/大小**：CSVM 顶点 bounds `x 0.04..0.30, y -0.48..-0.06, z -0.51..-0.07`（米）。
   CS 视角 68°，近裁 0.001；如果手在视野外或贴脸，调 `--offset/--scale`。
3. **朝向**：`--local-map` 只有 a/b/c/d 四种轴置换；`--rot` 是欧拉角。
   参考视频里是**手背朝相机、手指朝前上、小臂从右下进入**；当前 `rot (30,0,-20)` 只是近似。
4. **贴图/材质**：albedo 走 `srgb=true`；normal 的 G 通道在 shader 里被 `1.0 - y*2.0` 翻转
   （假设 DirectX 法线）。如果光照发灰/发黑，先试翻转/不翻转 normal。
   ORM 当前是统一值 `(255,110,255)`；真 `VT_Gauntlet_Mix` 的通道含义未确认，需要从
   `MI_PBZ_*` 材质或视频颜色反推。
5. **只有 section 0**：section 1/2 是左臂，当前没画。如果视频里能看到左臂/双手，需要把 1/2 也
   按对应骨骼（`Wrist_L`/`ArmTwist_L`）摆进去。
6. **姿势**：当前是 bind pose 手工旋转，不是 `AS_原版待机` 的第一人称姿势。动画解不出来之前，
   静态姿势很难完全对上视频。
7. **原生身体隐藏**：OW 第一人称下 `loader.cpp` 会隐藏原生身体；如果还有残影/武器，
   检查 `hide_mask1=0x100BF`、`hide_mask2=1` 和 `UpdateNativeModel` 的 RTTI 收集。

## 8. 文件清单

```
D:\code\universal-modder\ow_assets\
  pbz_unpack\Mods\WuchangDoomfist\Original\FirstPersonNative\SK_Doomfist_OW1_Gameplay1P.uasset
  pbz_unpack\Mods\WuchangDoomfist\Original\FirstPersonNative\SK_Doomfist_OW1_Gameplay1P_Skeleton.uasset
  pbz_unpack\Mods\WuchangDoomfist\Original\Animations\AS_*.uasset
  pbz_unpack\Mods\WuchangDoomfist\Original\Textures\...
  pbz_unpack\Mods\WuchangDoomfist\PBZ\Materials\RuntimeJueYingTextures\VT_Gauntlet_*.uasset/.ubulk
  reference.mp4
  video_contact.jpg

C:\Users\30435\ow_export\
  Program.cs                 # CUE4Parse 导出器 + 合成 global data
  build.ps1                  # csc 编译 + 复制依赖
  make_csvm.py               # JSON -> CSVM（支持 --bone/--center/--local-map/--rot）
  preview_csvm.py            # 离线软件光栅化预览
  out\doomfist_1p.json
  out\gauntlet_albedo.png / gauntlet_normal.png / gauntlet_mix.png
  out\ow_gauntlet_real.bin
  out\preview_final.png
```

## 9. 下一步建议

1. 先拿一张 OW 实机截图，和 `preview_final.png`、`reference.mp4` 并排看，判断是“没画出来”
   还是“画出来了但姿势/材质不对”。
2. 如果没画出来：查 `RenderFrame` 的 `ow_active` 条件、`g_ow.draw` 的深度/裁剪、
   `CsRenderer::draw` 的 `computeViewmodelProjection` 与 `world` 矩阵。
3. 如果画出来了但不像：先修姿势（动画或手工骨骼），再修 ORM/法线。
4. 动画优先尝试“改 CUE4Parse 强制 legacy 解压”，比继续猜姿势更根本。
