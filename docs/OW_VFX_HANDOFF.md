# OW 手炮火光 / 曳光原版 VFX 交接

日期：2026-11-02
Issue：https://github.com/icebear0828/mc-core/issues/136
状态：**进行中**。`SM_火花球` / `SM_曳光柱` 已成功导出为 OBJ（不再需要 PBZ 本体）；枪口火光 additive 切片已实现并编译通过，**待实机验证**；曳光柱和命中光源尚未接入。

## 1. 目标

把 PBZ mod 里的原版手炮 VFX 1:1 接到 Elden Ring 的 OW 模式：

- 枪口火光：`SM_火花球` 网格 + `MI_PBZ_手炮闪光`（Additive，基础色 `T_PBZ_HandFlash`，RGB 255/102/10）。
- 曳光：`SM_曳光柱` 网格 + `BP_手炮曳光`（蓝图里有静态网格组件和光源）。
- 命中：`BP_命中火花`（PointLight：Intensity 700，LightColor FFAF3C，AttenuationRadius 110）。

**验收**：火光贴在右手枪口、形状来自 `SM_火花球`；曳光是 3D 柱状光束而不是 2D 线；没有程序化星形/射线；音效 `ow.cannon.fire` / `ow.cannon.hit` 不变；CS 的 tracer 不受影响。

## 2. 资产位置

mod pak（UE5.6，Phantom Blade Zero）：

```
D:\code\universal-modder\影之刃零_第一人称末日铁拳MOD\1_100_P.{pak,ucas,utoc}
```

已解包副本：

```
D:\code\universal-modder\ow_assets\pbz_unpack\Mods\WuchangDoomfist\
  FX\BP_手炮曳光.uasset
  FX\SM_曳光柱.uasset / .ubulk
  FX\SM_火花球.uasset / .ubulk
  FX\BP_命中火花.uasset
  PBZ\Materials\MI_PBZ_手炮闪光.uasset          # BLEND_Additive
  PBZ\Materials\Textures\T_PBZ_HandFlash.uasset  # 32x32 纯色 255/102/10
```

已导出的贴图：`C:\Users\30435\ow_export\out\handflash.png`（纯色，只提供颜色，不提供形状）。

## 3. 已经试过、失败的路（结论已更正）

1. **最初的 `Invalid bool value (-1458592069)` 不是缺 PBZ 本体。**
   - 真正原因：`ow_export/Program.cs` 的 `InjectEmptyGlobalData` 把 export-map 的 class hash 标错了。`SM_火花球` 有三个导出，class index 分别是
     `0x2623523DEF01A2F7` = `BodySetup`、`0xCFA78A7B1C2695` = `NavCollision`、`0x7CDE1A593E47CF` = `StaticMesh`。
   - 旧代码把 `0x2623...` 当成 `StaticMesh`，于是拿 `UStaticMesh` 去解 `BodySetup_8`，在属性流里读到垃圾 bool。
   - 修正：注入正确的 class 名，并让 `--staticmesh` 遍历导出、按 `UStaticMesh` + 名字选中，而不是永远取 `ExportsLazy[0]`。
2. **UE5.6 尾部 `FPerPlatformFloat` 错位。**
   - `UStaticMesh` 的 LOD 数据在 99% 处解析完后，`FStaticMeshRenderData` 的 `ScreenSize` 数组读到 `-1`（`TPerPlatformProperty` 的 cooked bool）。
   - 处理：加 `--staticmesh-file` 直接读松散 `.uasset`，把 `ScreenSize` 起始处的 8×8 字节改成 `01 00 00 00 00 00 00 00` 让解析走完；LOD 顶点/索引在此之前已正确解析。
   - 产物：`SM_火花球` 559 verts / 960 tris，半径 50 的球；`SM_曳光柱` 334 verts / 512 tris，半径 50、长 100 的柱体。
3. **FModel / UEViewer**：仍然没有安装，不再需要。

## 4. 推荐做法

### 4.1 拿到网格（已完成）

不再需要 PBZ 本体或 FModel：

```powershell
cd C:\Users\30435\ow_export
.\bin\ow_export.exe --staticmesh-file <patched.uasset> out.obj
```

`Program.cs` 现在支持 `--staticmesh-file`（松散 uasset + 同名 `.ubulk`）和修正后的 class 注入。
导出的 OBJ 用 `uv run python tools/ow/build_vfx_mesh.py in.obj ow_vfx_<name>.bin` 转成 OWVF（40 字节顶点 + uint32 索引），
部署工具会把 `ow_vfx_spark.bin` 拷到 `mods\mc_adapter\`。

### 4.2 运行时 additive 3D 通道（枪口火光已实现）

主要文件：`adapters/eldenring/src/ow_renderer_d3d12.cpp` / `.hpp`

- 复用现有 root signature（40 个 root constants + 3 张 SRV）。
- `vfx_pso_`：`SrcBlend=ONE`、`DestBlend=ONE`、`DepthWriteMask=ZERO`、`CullMode=NONE`，使用新的 unlit `PSVfx`。
- `loadVfxMesh()` 读 OWVF；`muzzle_bone_` 在 `loadModel()` 里按 `IndexFinger_3_L` → `Wrist_L` → `IndexFinger_2_L` 解析。
  - **注意：开火的是左手，不是右手。** 大金属拳套（`section_0` / `mat 0`）由 `*_R` 骨骼驱动、在屏幕右侧，是 Rocket Punch 用的；手炮是屏幕左侧那条手臂（`section_2` / `mat 2`，92.5% `*_L`）。`shoot` 动画里左手从屏幕 (0.34,0.74) 移到准星附近 (0.53,0.59)，右手拳套反而移出右侧画面。
  - Issue 原文写的 “right-hand muzzle” 是错的。
- `draw()` 在拳套之后、`since_shot < kMuzzleFlashTime (0.09s)` 时，把球体放在 muzzle 骨骼的 palette 平移处，
  tint = `T_PBZ_HandFlash` 的 255/102/10，按 `1 - t/0.09` 淡出。
- 实机快捷键：PageUp 切换 additive pass 的深度测试，PageDown 循环缩放 0.002/0.004/0.006/0.008/0.012。
- 离线验证：`ow_assets/` 里的 CSVM 用 `tools/ow` 同款矩阵约定做过软件光栅预览，左手 idle 和 shoot f4 都能看到火光；
  射线测试（idle f0 / shoot f4 / shoot f8）显示 20 cm 球体前缘都穿出手臂表面，深度开启也可见。
- 待办：曳光柱（世界空间，用 `g_fx_vp` 和命中点画柱体）、命中 PointLight 近似。

### 4.3 代码指针

| 位置 | 作用 |
|---|---|
| `ow_renderer_d3d12.cpp` `init()` / `draw()` | root signature、PSO、纹理、CPU 蒙皮、每帧绘制 |
| `overlay_scene.cpp` | 每帧 `g_ow.setConfig/tick/draw`，viewmodel 投影 |
| `overlay_hud.cpp` | 2D HUD；CS 的 `DrawCsMuzzleFlash` / `DrawTracers` 可参考；OW 自绘火光已删除 |
| `combat_service.cpp` `fireOwStrike()` | OW 命中/伤害；枪口/曳光钩子已删除 |
| `overlay_d3d12.cpp` | `SpawnFx` / `SpawnTracer` |
| `include/mc/particles.hpp` | 粒子种类（`Muzzle` 已删除） |
| `ow_export/Program.cs` | `--staticmesh`（当前失败）、`--dump`、`--anim`、`InjectEmptyGlobalData` |

## 5. 当前基线

- 构建：`mc-core/build_modes/mc+cs+ow`（`ENABLE_MC_MODE` / `ENABLE_CS2_VIEWMODEL` / `ENABLE_OW_VIEWMODEL` 全 ON）。
- 测试：`mc_tests` 1350 passed / 1 skipped（`MC_OW_ASSET` 未设置时 skipped）。
- 部署：`Game\dinput8.dll` SHA256 `DED9EB0808110B5CED62CC9C590C597B293509DD6C4E73B3ACAC7CFC61E5FA27`。
- 已保留：拳套双手、100 骨骼、5 段动画；9 个原版音效（含循环蓄力）；伤害、冷却、顿帧、位移、震屏。
- 已删除：自绘 2D 火光、2D OW tracer、Muzzle 粒子。

## 6. 复现/验证

```powershell
# 构建 + 部署
cd D:\code\universal-modder
uv run python tools/deploy_modes.py --modes all --deploy --yes

# 测试
mc-core\build_modes\mc+cs+ow\bin\RelWithDebInfo\mc_tests.exe

# 进游戏：Shift+F7 进 OW，左键开火
# 日志：Game\mc_er.log，关注 OW-STRIKE / OW-DASH
```
