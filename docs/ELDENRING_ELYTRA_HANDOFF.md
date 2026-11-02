# 鞘翅（Elytra）渲染交接文档

写给接手的人：模型数据我已经按 **1.21.8 真实客户端二进制**逐项核对过，可以当事实用。
剩下的问题需要**能看画面的人**定位——我没法看游戏画面，只在猜。

---

## 0. 一句话现状

- 几何 / 姿态 / 放置 / UV / 贴图：**已与 1.21.8 客户端逐项核对一致**（证据见 §2）。
- 站立姿态下翼面出现 **"透明的渣滓"**（用户原话）：**未解决**。
- 工作区改动**未提交**。

---

## 1. 分支与状态

| 项 | 值 |
|---|---|
| worktree | `D:\code\universal-modder\mc-core-armor` |
| 分支 | `feat/steve-armor-elytra` |
| HEAD | `23fceb4 fix(eldenring): use the vanilla folded and gliding elytra poses` |
| 测试 | `1307 tests from 171 test suites ran. [ PASSED ] 1307 tests.` |
| 已部署 DLL | `dinput8.dll` SHA256 `856357a0fc4214b428acc4760ce24246570d18d021de43aa62c94301a31c5982` |

未提交改动：

```
 M adapters/eldenring/src/overlay_scene.cpp
 M adapters/eldenring/src/ui/hud_overlay.cpp
 M assets/source/models/entities/elytra_wings.geo.json
 D assets/source/models/entities/elytra_wings_spread.geo.json
 M include/mc/entity_model.hpp
 M src/entity_model.cpp
 M tests/test_elytra_trident_models.cpp
 M tests/test_entity_model.cpp
?? assets/source/models/entities/elytra_wings_crouch.geo.json
?? assets/source/models/entities/elytra_wings_glide.geo.json
```

---

## 2. 已验证事实（有二进制依据，不是推导）

### 2.0 证据来源

`D:\game\sekiro\build\mc_client_1.21.8.jar`（29,525,242 B，与 Mojang `1.21.8.json` 里 `downloads.client.size` 完全一致，即官方 client.jar）。
类名混淆，用**字符串常量**定位：扫描所有 `.class` 的常量池，找含 `"left_wing"` 的类 → `gnd.class`（鞘翅模型）、`hjg.class`（渲染层）。
反汇编：`C:\Program Files\Java\jdk-24\bin\javap.exe -c -p -constants gnd.class`。

### 2.1 几何（`gnd.createLayer`，javap 原文）

```
ldc "left_wing"
  bipush 22 / iconst_0                       -> texOffs(22, 0)
  ldc -10.0f, fconst_0, fconst_0, ldc 10.0f, ldc 20.0f, fconst_2, aload_2(CubeDeformation 1.0f)
  ldc 5.0f, fconst_0, fconst_0, ldc 0.2617994f, fconst_0, ldc -0.2617994f
                                             -> PartPose.offsetAndRotation(5, 0, 0, 15°, 0, -15°)

ldc "right_wing"
  bipush 22 / iconst_0
  invokevirtual gqs.a()                      -> CubeListBuilder.mirror()
  fconst_0, fconst_0, fconst_0, ldc 10.0f, ldc 20.0f, fconst_2
  ldc -5.0f, fconst_0, fconst_0, ldc 0.2617994f, fconst_0, ldc 0.2617994f
                                             -> PartPose.offsetAndRotation(-5, 0, 0, 15°, 0, 15°)

bipush 64 / bipush 32                        -> LayerDefinition.create(mesh, 64, 32)
```

### 2.2 姿态（`gnd.setupAnim` + `bzl` 常量池）

```
leftWing.y   = state.Q(crouching) ? 3.0f : 0.0f
leftWing.xRot= state.V ; zRot = state.X ; yRot = state.W
rightWing.yRot = -leftWing.yRot
rightWing.y    =  leftWing.y
rightWing.xRot =  leftWing.xRot
rightWing.zRot = -leftWing.zRot
```

`bzl` 里算 V/W/X：

```
ldc 0.2617994f  ldc 0.34906584f     -> lerp(15°, 20°)        // 静止 -> 滑翔
ldc -0.2617994f ldc -1.5707964f     -> lerp(-15°, -90°)
crouch: 0.6981317 / -0.7853982 / 0.08726646  -> 40° / -45° / 5°
```

### 2.3 放置（`hjg`，常量池只有一个浮点 `0.125f`）

```
invokevirtual fod.a:()V
fconst_0, fconst_0, ldc 0.125f, invokevirtual fod.a:(FFF)V   -> poseStack.translate(0, 0, 0.125)
```

### 2.4 UV 角点顺序（`ModelPart$Polygon`，1.20.1 与 1.21.11 一致）

```java
vertices[0] = vertices[0].remap(u1, v0);
vertices[1] = vertices[1].remap(u0, v0);
vertices[2] = vertices[2].remap(u0, v1);
vertices[3] = vertices[3].remap(u1, v1);
if (mirror) { 反转顶点数组; }   // remap 返回新 Vertex，所以反转只改绕序，不改 UV
```

UV 矩形（`ModelPart$Cube`，`i`=texOffsX, `j`=texOffsY, `dx/dy/dz`=尺寸）：

| 面 | 矩形 |
|---|---|
| WEST (-X) | `[i, i+dz]` × `[j+dz, j+dz+dy]` |
| NORTH (-Z) | `[i+dz, i+dz+dx]` × `[j+dz, j+dz+dy]` |
| EAST (+X) | `[i+dz+dx, i+2dz+dx]` × `[j+dz, j+dz+dy]` |
| SOUTH (+Z) | `[i+2dz+dx, i+2dz+2dx]` × `[j+dz, j+dz+dy]` |
| DOWN (-Y) | `[i+dz, i+dz+dx]` × `[j, j+dz]` |
| UP (+Y) | `[i+dz+dx, i+2dz+dx]` × `[j, j+dz]` |

### 2.5 贴图

`assets/minecraft/textures/entity/equipment/wings/elytra.png`（jar 内）与游戏目录的
`mods\mc_adapter\elytra_wings.png` **SHA256 完全一致**：`6ADBBC0F7D39CD233083F672A6624141D3DE47885EFDD1F387D50E92C75EEB2D`。

内容：64×32，**只有 alpha 0 和 255**，没有半透明像素。翼面是深蓝灰 `424C58` → 浅蓝灰 `7F8F9F` 的渐变，
形状是斜的叶形（上窄下宽），四角透明。NORTH 面（朝向身体那面）**整块全透明**。

---

## 3. 已做的代码改动

### 3.1 `src/entity_model.cpp`

| 位置 | 改动 |
|---|---|
| `cubeFaces()` L268 | UV 矩形改成 §2.4 的 west/north/east/south 布局（原来是 east/north/west/south，且 up/down 对调）；`down` 面的角点顺序改成原版 `Vertex[]` 顺序 |
| `appendCubeMesh()` L322 | `s_of = {1,0,0,1}`、`t_of = {0,0,1,1}`（原来 `{0,1,1,0}`/`{1,1,0,0}`，**整体转了 180°**） |
| `boneRestRotation()` L393 | 欧拉序改成 **Z→Y→X**（原来 X→Y→Z）。Blockbench 的 Bedrock 格式默认 `euler_order: 'ZYX'`，与 Java 的 `Quaternionf.rotationZYX` 一致 |

> **这三条影响所有实体模型**（苦力怕/僵尸/骷髅）。它们的贴图左右对称，所以之前没暴露。
> 改完透明问题消失了（用户确认："不透明了，好多了"）。

### 3.2 模型文件 `assets/source/models/entities/`

- `elytra_wings.geo.json` — 站立（`15, 0, ∓15`）
- `elytra_wings_glide.geo.json` — 滑翔（`20, 0, ∓90`）
- `elytra_wings_crouch.geo.json` — 潜行（`40, ±5, ∓45`，轴心 y=21、方块 origin y=1）
- 删除 `elytra_wings_spread.geo.json`

Bedrock 坐标换算：`bedrock_y = 24 - java_y`；`+Z = 后`；方块 `origin` 是模型空间绝对值。
右翼方块 origin z = **2.25**（不是 2.0）——这是刻意的 0.25px 深度偏移，用来打破两片共面翅膀的深度平局。

### 3.3 渲染侧

| 文件 | 改动 |
|---|---|
| `adapters/eldenring/src/overlay_scene.cpp` L308-316 | 按 `gliding / sneaking` 选模型；删掉自造的 `flap` 摆动（原版没有这个动画） |
| `adapters/eldenring/src/ui/hud_overlay.cpp` L319 | 加载列表改成 `{"elytra_wings", "elytra_wings_glide", "elytra_wings_crouch", "trident"}` |

### 3.4 测试

- `tests/test_entity_model.cpp`：`BoxUvLayoutMatchesModelPartCube`、`AMirroredCubeFlipsEveryFaceHorizontallyWithoutMovingTheRectangles`、`CreeperHeadFaceUvOrientationMatchesMinecraftStandard`、`ARestRotationTurnsZFirstThenYThenX`
- `tests/test_elytra_trident_models.cpp`：几何/姿态/UV 断言

---

## 4. 遗留问题：翼面"透明渣滓"

### 4.1 症状

用户原话：「**就是那种有透明的渣滓一样**」「**右翼显示不对劲**」。
站立、装备鞘翅、第三人称从背后看时出现。**具体位置（中间/边缘/整片）未知**——这是下一步要确认的第一件事。

### 4.2 候选根因（按可能性排序）

**A. 两片翅膀共面 → 深度打架**

两片的 `xRot` 相同、`zRot` 互为相反数，所以它们**落在同一个平面上**、各自铺满整个后背。
原版靠 `bodyParts()` 先左后右 + `GL_LEQUAL` 让后画的稳定盖住先画的；我们的管线是 `D3D12_COMPARISON_FUNC_LESS` + 浮点深度，
理论共面实际差几 ULP，会逐像素互抢 → 棋盘/噪点。

已经加的缓解：右翼方块 origin z = 2.25（0.25px ≈ 1.4cm）。

- **验证**：把 2.25 临时改成 3.0（≈5.6cm，肉眼仍不明显），如果渣滓消失 → 就是这条，需要换更稳的方案
  （例如给翅膀单独一个 depth bias，或按原版语义让后画的赢）。
- **更狠的验证**：临时只画左翼（`DrawProp` 里把 rightWing 的 cube 去掉），如果渣滓消失 → 确认是两片互抢。

**B. 场景深度遮挡把翼面像素剔掉**

`adapters/eldenring/shaders/steve.hlsl` L45-83 `PSMain`：

```hlsl
if (scene.w > 0.5) {
    ...
    if (steve_z > near_skip && gd > 0.0 && scene.x / gd < steve_z * (1.0 - scene.y) - scene.z) discard;
}
```

`DrawProp` 把 `g_elytra_table_gpu`（slot 28 深度副本 + slot 29 贴图）作为 SRV 表传进去，`pp.keep_depth = true`。
如果 slot 28 的深度副本是**过期的**或者拿到的是**原生角色**的深度，翼面（在躯干后 2px）会被逐像素剔掉 → 看起来像透明的渣。

- **验证**：把 `SteveParams.mode` 临时设 0（关遮挡）再画翅膀。渣滓消失 → 是这条。
- 注意：`CreateElytraTexture`（`overlay_textures.cpp` L271）里先 `setDepthView(device, nullptr, depth_slot)`，
  靠之后的 `g_depth_dirty` 才绑定真实深度（`overlay_scene.cpp` L64-76）。要确认这条路径确实跑到了。

**C. 贴图上传 / 采样**

- 采样器是 point + clamp（`steve.hlsl` L11），贴图 alpha 只有 0/255，理论上不会半透明。
- `overlay_textures.cpp` L271 `CreateElytraTexture` 走 `g_steve.setMobSkin(device, "elytra_wings", rgba, 64, 32, tex_slot)`，
  要确认 RGBA 通道顺序与 `CreateSkinTexture` 的 `DXGI_FORMAT` 一致（B8G8R8A8 vs R8G8B8A8）。
  通道错位会表现为**颜色偏蓝/偏红**，不会产生透明；但如果是 alpha 通道丢失，整片会消失——用户能看到翅膀，所以基本排除。
- **验证**：临时把 `pp.tint` 设成不透明纯色（`use_texture = false`，tint.a = 1），如果渣滓还在 → 与贴图无关。

### 4.3 建议的定位流程

1. **先拿到截图**：游戏里 F7 存图（存到游戏根目录 `mc_er_shot_NNN.png`），或让游戏开着让人直接抓屏。
2. 按 §4.2 的 A/B/C 顺序做**单变量**验证，每次只改一处、每次截图对比。
3. 定位后再决定修法，不要一次改多个变量。

---

## 5. 构建 / 测试 / 部署

```powershell
# 配置（本 worktree 的 build/ 已配好：ENABLE_MC_MODE=ON, CS2/OW=OFF）
cmake -S . -B build -DBUILD_TESTING=ON

# 测试
cmake --build build --config RelWithDebInfo --target mc_tests
.\build\bin\RelWithDebInfo\mc_tests.exe

# 适配器
cmake --build build --config RelWithDebInfo --target eldenring_adapter
# 产物：build\bin\eldenring\dinput8.dll

# 部署（游戏必须关：进程名是 start_protected_game.exe）
$g = "C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game"
Copy-Item build\bin\eldenring\dinput8.dll "$g\dinput8.dll" -Force
Copy-Item assets\source\models\entities\elytra_wings*.geo.json "$g\mods\mc_adapter\models\" -Force
certutil -hashfile "$g\dinput8.dll" SHA256
```

模型 JSON 是**启动时**加载的，改模型要重开游戏；改 DLL 也要重开。

---

## 6. 踩过的坑（别重犯）

1. **`Set-Content -Encoding utf8`（Windows PowerShell 5.1）会写 BOM**，C++ 解析器读不了 → 模型不加载、测试红。
   用 `[System.IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding($false)))`。
2. **不要用自己写的渲染器验证自己写的模型**——同一个误解会同时污染两边，看起来"完全一致"。
   我因此误判过两次。要用游戏截图或第三方实现。
3. **`mirror` 语义有歧义**：`ModelPart$Polygon` 里 `mirror()` 只反转顶点数组（绕序）+ 翻法线，**不改 UV**；
   但 Bedrock 的 `"mirror": true` 在我们的解析器里是翻 UV 的。当前模型右翼**没有**开 mirror（`mirror: false`）。
   用户反馈去掉 mirror 后"右翼显示不对劲"、加回 mirror 后又出现透明——这条没定论，需要看图判断。
4. **一次只改一个变量**。我连着改 UV 布局、UV 角点、mirror、z 偏移，问题来回震荡，浪费了大量时间。
5. `steve.gliding` 目前**只由调试键**（Insert，`dbg_glide`）赋值，见 `hud_overlay.cpp:137`；
   `hud_bridge.cpp` 里没有赋值，`ctx.mode().flying` 也没接。**正常游玩时滑翔姿态触发不到**，翅膀永远是折叠的。
   要接的话：`hud_bridge.cpp` 里根据实际飞行状态设置 `steve.gliding`。

---

## 7. 附：离线预览脚本

`C:\Users\30435\AppData\Local\Temp\elytra_cmp\`（临时目录，不在仓库里）：

- `render.py` — 用真实贴图渲染「原版 UV 规则」vs「我们的 UV 规则」四联图
- `body.py` — 把翅膀套在史蒂夫身体上，出后/侧/前三视图
- `dumpclass.py` / `findpose.py` / `findref.py` — 在客户端 jar 里按字符串常量定位混淆类并 dump 常量池

跑法：`uv run --with pillow python <script> <elytra_wings.png> <out.png>`。

注意：这些脚本的 UV 规则是我从 C++ 抄过去的，**只能验证两边是否一致，不能证明和原版一致**（见坑 2）。
