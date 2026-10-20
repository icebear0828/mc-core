# 地面采掘与数据驱动掉落表（issue #66 / #81）

最后更新：2026-10-20
分支：`feat/mining-loot-tables`（基于 `main` `5fe648d`）
PR：https://github.com/icebear0828/mc-core/pull/115
关联：issue #66（就地采掘）、issue #81（矿物与常用方块掉落表）

## 1. 这次做了什么

把"低头对地面长按左键 → 10 阶裂纹 → 按区域权重掉落方块/矿物 → 自动进背包"的核心链路落地：

| 层 | 文件 | 内容 |
| :--- | :--- | :--- |
| 核心掉落 | `include/mc/loot_table.hpp` | 6 区域 × 12 条目的权重表、工具门槛、保底、矿脉连击、可注入的确定性 RNG |
| 核心采掘 | `include/mc/ground_mining.hpp` | 俯仰角/手持物门槛、裂纹 0~9、破坏时间、掉落触发 |
| 适配层 | `adapters/eldenring/src/loader.cpp` | `StepMining` 接地形射线、相机俯仰角、区域配置、掉落入包 |
| 物品 | `include/mc/types.hpp`、`src/hud.cpp` | 11 种矿物/粗矿 + 圆石 + 5 种镐的 `ItemId` 与显示名 |
| 图标 | `tools/extract_mc_assets.py`、`include/mc/hud_atlas.hpp` | 11 个 16×16 图集格子，按追加顺序生成，旧 UV 不变 |
| 测试 | `tests/test_loot_table.cpp`、`tests/test_ground_mining.cpp`、`tests/test_mining_assets.py` | 算法、门槛、时序、蒙特卡洛与图集校验 |

## 2. 触发条件

一次采掘必须同时满足：

1. `mc_er_steve.txt` 里 `blocks=1`（地面采掘复用方块系统的开关）。
2. 视线俯仰角 `>= 60°`（`GroundMiningController::kMinPitchDeg`），即视线落在脚下附近。
3. 主手为空，或手持任意镐（木/石/铁/金/钻石/下界合金）。
4. 准星命中宿主静态地形，而不是我们放置的方块；命中面法线必须朝上（`normal.y >= 0.5`），
   墙/岩石侧面不算地面，不会生成裂纹或触发采掘。命中放置方块时仍走原来的渐进破坏逻辑。
5. 松开左键、抬头、切换工具、切换区域或**玩家开始移动**会立即清空裂纹进度；保底/连击计数保留。
   移动判定读 `PhysicsModule+0x120` 的线速度（HANDOFF §7 的 A 级事实），速度模长超过 0.5 m/s 即视为移动；
   用速度而不是位置差，避免浮动原点重定位（Havok 空间按 8 m 跳变）被误判成移动。

手持剑、弓、盾、食物等非镐物品时，低头长按不会触发地面采掘，仍走宿主原生攻击/交互。

## 3. 掉落表

默认表是 `mc::kDefaultLootTable`（12 行 × 6 区域），权重为每列的百分点。掷骰时按该列实际总和归一化，因此不要求每列严格等于 100。

| 产出物品 | ① 草地/泥土 | ② 石地/城堡 | ③ 矿洞 | ④ 结晶坑道 | ⑤ 火山 | ⑥ 深根 |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 泥土 (`BlockDirt`) | 90.0 | 2.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| 圆石 (`BlockCobblestone`) | 5.0 | 76.0 | 35.0 | 25.0 | 15.0 | 20.0 |
| 煤炭 (`Coal`) | 2.0 | 8.0 | 18.0 | 5.0 | 10.0 | 12.0 |
| 粗铜 (`RawCopper`) | 1.5 | 5.0 | 15.0 | 5.0 | 5.0 | 8.0 |
| 粗铁 (`RawIron`) | 1.0 | 6.0 | 20.0 | 10.0 | 15.0 | 22.0 |
| 红石 (`Redstone`) | 0.0 | 1.5 | 5.0 | 12.0 | 12.0 | 10.0 |
| 粗金 (`RawGold`) | 0.0 | 1.0 | 4.0 | 6.0 | 20.0 | 8.0 |
| 青金石 (`LapisLazuli`) | 0.0 | 0.3 | 1.5 | 15.0 | 2.0 | 8.0 |
| 紫水晶 (`AmethystShard`) | 0.0 | 0.0 | 0.0 | 18.0 | 0.0 | 2.0 |
| 钻石 (`Diamond`) | 0.0 | 0.2 | 1.5 | 3.5 | 1.0 | 4.0 |
| 绿宝石 (`Emerald`) | 0.5 | 0.0 | 0.0 | 0.5 | 0.0 | 6.0 |
| 远古残骸 (`NetheriteScrap`) | 0.0 | 0.0 | 0.0 | 0.0 | 0.5 | 0.0 |

### 3.1 数据问题：火山列合计 80.5

issue #81 的火山列相加是 **80.5**，不是 100（缺 19.5 个百分点）。代码没有擅自改数，而是按实际列总和归一化，所以火山列仍可用，但各项实际概率会被放大 `100 / 80.5 ≈ 1.24` 倍。需要产品或数据同学确认缺失的 19.5 应该分给哪些条目；确认后只改 `kDefaultLootTable` 的数值即可。

另外，issue 表里"泥土/沙土"和"圆石/深板岩"是合并行，当前实现分别只落到 `BlockDirt` 与 `BlockCobblestone`；沙、深板岩、花岗岩等尚未拆成独立条目。

## 4. 工具门槛

| 工具 | 可正常采集 | 抽到更高阶条目时 |
| :--- | :--- | :--- |
| 空手 / 木镐 | 泥土、圆石、煤炭 | 降级为圆石（"破损石屑"），不重掷 |
| 石镐 | 上述 + 粗铜、粗铁、青金石 | 降级为圆石 |
| 铁镐及以上 | 全部，包括粗金、红石、紫水晶、钻石、绿宝石、远古残骸 | — |

"铁镐及以上"当前包括金镐（枚举顺序 `Iron < Gold`）。这与原版 MC 的金镐定位不同，是按 issue #81 的"铁镐及以上完全解锁"实现的；若要对齐原版，需要把金镐降到石级。

降级而不是重掷，是为了保持整张表的概率分布不被工具等级扭曲。蒙特卡洛测试里空手在矿洞只会得到煤炭或圆石，且煤炭约 18%。

## 5. 保底与矿脉连击

- **保底（pity）**：只在地下区域（矿洞/结晶坑道/火山/深根）生效。连续 5 次没出粗铁后，粗铁权重每再失败一次 +15 个百分点，最多 +75；出铁或回到地表清零。因工具门槛被降级的粗铁按"没出铁"计数。
- **矿脉连击（combo）**：地下连续第 4 次采掘时，主掉落只在矿物条目里抽（必出 1 组矿物）；随后 50% 概率追加煤炭或红石（红石需要铁镐及以上）。触发后连击清零，回到地表也清零。
- 两者都存在于 `LootTable` 内，`GroundMiningController::reset()` 只清裂纹，不会清掉保底/连击。

## 6. 破坏时间与裂纹

基础时间（空手 → 下界合金镐）：

| 工具 | 空手 | 木 | 石 | 铁 | 金 | 钻石 | 下界合金 |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 秒 | 2.5 | 2.0 | 1.75 | 1.5 | 1.25 | 1.0 | 0.9 |

再乘区域硬度：草地 1.0、石地 1.3、矿洞 1.5、结晶坑道 1.7、火山 1.8、深根 1.6。创造模式瞬时破坏。

裂纹阶段 0~9 与 `MiningController` 一致，每 0.25 s 触发一次打击音效；破坏时播放拾取音效并把掉落物放进背包（优先叠加已有堆，再找空位，快捷栏优先）。

## 7. 配置

在游戏目录的 `mc_er_steve.txt` 里：

```ini
blocks=1          # 必须；地面采掘复用方块系统开关
ground_mining=1   # 默认 1，设为 0 关闭地面采掘
loot_zone=0       # 0..5 手动指定；-1 = 自动（玩家 block id + 地面材质）
ground_crack_lift=-0.05  # 裂纹沿法线的抬升（米）；负值往下压，默认 0.01
ground_crack_size=0.9    # 裂纹四边形边长（米），默认 0.9
# 自动模式下的可选覆盖（0..5 同下表）：
# zone_mat_<material_id>=<zone>   # 材质优先，例如 zone_mat_1234=4
# zone_area_<area_id>=<zone>      # 材质没有规则时按 block id 的 area，例如 zone_area_60=0
```

`loot_zone` 取值：

| 值 | 区域 | 枚举 |
| ---: | :--- | :--- |
| 0 | 草地/泥土 | `LootZone::Grassland` |
| 1 | 石地/城堡 | `LootZone::StoneSurface` |
| 2 | 常规矿洞 | `LootZone::Cave` |
| 3 | 结晶坑道 | `LootZone::CrystalCave` |
| 4 | 火山熔岩地底 | `LootZone::Volcano` |
| 5 | 深根/永恒之城 | `LootZone::Deeproot` |
| -1 | 自动 | 读 `PlayerIns+0x6D0` 的 block id 与射线命中的材质 |

自动模式的默认规则：先查 `include/mc/er_region_table.hpp` 的 **map tile 表**（由 Smithbox 的 ER map ID 列表生成，463 个 `m<area>_<gx>_<gz>_<lod>` 块），再查 area 表（`area 60 -> Grassland`、`area 61 -> Cave`、其他 -> `StoneSurface`），最后回退；材质规则优先于两者。`zone_mat_*`/`zone_area_*` 就是给实机探针拿到材质 id 后调参用的。

在游戏目录放一个空的 `mc_er_zoneprobe.txt` 会打开逐条材质日志（只读，限频 200 ms）：

```text
ZONEPROBE: block=0x3C2A2400 area=60 gx=42 gz=36 lod=0 pos=(...) n=(...) shape=0x... matidx=... material=...
```

材质路径与 block id 的字节依据见 `ELDENRING_REVERSE.md` §45。自动区域目前是**静态路径已确认、实机材质 id 未采集**的状态：先用探针跑草地/石地/矿洞/结晶/火山/深根，再把 `zone_mat_*` 固化。

实机（2026-10-20，Limgrave Church of Elleh）已采集到：`material=1` 出生点石台（掉圆石/煤炭/粗铁），`material=2` 和 `material=5` 草地/泥土（掉泥土/圆石）。配置里是 `zone_mat_1=1`、`zone_mat_2=0`、`zone_mat_5=0`；material 值不是全局语义，其他区域仍需分别采样。

实机调裂纹高度：游戏在前台时，`[` / `PageDown` 下移 5 cm，`]` / `PageUp` 上移 5 cm；`,` / `.` 精调 1 cm。每次调整写一行 `cracktune: lift=...`，调到贴地后把该值写进 `ground_crack_lift`。裂纹贴在碰撞面上，视觉地板可能略低；往下压越过视觉地板会被深度测试挡住，所以贴近地面时用 1 cm 精调。

## 8. 测试与验证

在 `feat/mining-loot-tables` 上：

```bash
cmake --build build_pure_mc --config Release -j 8
./build_pure_mc/bin/Release/mc_tests.exe --gtest_brief=1
# [==========] 1165 tests from 160 test suites ran.
# [  PASSED  ] 1165 tests.

uv run --with pillow --with pytest python -m pytest tests/test_mining_assets.py -q
# 8 passed
```

覆盖点：

- 默认表各区域权重和（火山 80.5 有专门断言）
- 矿洞 100,000 次蒙特卡洛：粗铁落在 19%~21%，钻石约 1.5%
- 空手/石镐的门槛与降级行为
- 保底计数、连击触发、地表清零
- 俯仰角 60° 边界、非镐物品不触发
- 移动立即打断、移动时无法开始、打断不清空保底/连击
- 裂纹阶段推进、松键/抬头/换工具重置
- 创造模式瞬时破坏
- 11 个新图集格子的尺寸与 UV
- 5 个镐图集格子的注册、尺寸与 UV，以及手持模型样式
- block id 解码（area/gx/gz/lod）、材质规则优先于 tile/area 规则、未知输入回退
- ER map tile 表的已知块：Limgrave、Volcano Manor、Sellia Crystal Tunnel、Deeproot、Stormveil、Catacombs
- 地面裂纹四边形：水平/垂直法线、抬高 1 cm、UV 落在对应 `destroy_stage_N`、无 stage 时不生成

**未做**：没有部署 DLL，也没有在游戏里实机验收；`ZONEPROBE:` 还没有真实游戏数据。部署/实机验证留给下一次会话。

## 9. 已知限制与下一步

1. **区域自动识别**（**静态路径已完成，待实机材质 id**）：玩家 block id 走 `PlayerIns+0x6D0`
   （`PlayerIns` vtable `0x2A7FBB0`，构造函数 `0x650C90` 初始化，更新 `0x65AD00` 写入），格式
   `area<<24 | gx<<16 | gz<<8 | lod`；地面材质由射线命中回调 `0xC5A8E0` 的 `hit+0x48/+0x50`
   经 `0xC75570` 解析。`loot_zone=-1` 已接自动分类，`mc_er_zoneprobe.txt` 输出 `ZONEPROBE:`
   日志。剩下的是实机采集六个区域的 material id，再用 `zone_mat_<id>=<zone>` 固化；area 与地图
   区域名（Limgrave/Liurnia/…）的对应表也还没做。证据见 `ELDENRING_REVERSE.md` §45。
2. ~~**地面裂纹与碎屑粒子**~~（**已完成，待实机验收**）：复用方块挖掘的 `destroy_stage_0..9`
   贴图，在**视觉地面**上按法线贴一个 0.9 m 的裂纹四边形（`TerrainMiningOverlay`，抬高量可配）；
   每 0.25 s 打击和破坏时调用现有 `FxKind::Dust` 粒子（破坏时 3 连发）。视觉地面来自**场景深度**：
   overlay 把玩家脚底投影到屏幕，在脚底下方约 2% 屏高处取场景深度 texel，用 shader 的
   `depth_const / depth` 还原视空间 z，再沿该像素的相机射线得到世界坐标。**不能取准星中心**：第三人称
   下准星落在 Steve 身体上，中心 texel 是身体后面的地面，裂纹会被身体挡住并显得偏高。深度不可用时回退
   到准星碰撞命中点。Havok 碰撞射线只用于掉落/材质判定——它可能打在一个看不见的碰撞台/台阶上，这正是
   之前裂纹悬空、往下压又被挡住的原因。`GROUNDANCHOR:` 日志现在带 `floor=(...) scene=0/1`。裂纹是
   **独立网格**，用 `rel_bias=0`、`abs_bias=0.001` 绘制：方块用的遮挡偏置（8% + 5 cm）会逼着贴地裂纹
   往前浮才不被场景深度丢弃，这正是「要么太高、要么往下就消失」的根因。石/土/木/金属的**不同颜色**碎屑
   还没分，目前统一是灰色 Dust。
3. **`random.pop` 音效**：提取音效列表里没有这个事件，拾取暂时用 `entity.experience_orb.pickup`。要完全还原可把 `random.pop` 加进 `tools/extract_mc_sounds.py` 的 `EVENTS` 后重新提取。
4. ~~**镐的图标**~~（**已完成**）：木/石/铁/金/下界合金镐各自有 16×16 图集格子（`item_*_pickaxe`），
   来源是 client.jar 的 `item/*_pickaxe.png`；ER/Sekiro 的 `uvForItem` 与手持模型样式（`Handheld`、平面精灵）
   都已接。占位图集里是彩色圆点，实机部署前仍需从 jar 重新提取完整图集。
5. **合并行拆分**：沙、深板岩、安山岩、花岗岩、黑曜石等常用方块还没拆成独立条目；圆石也还不能右键放置。
6. ~~**移动打断**~~（**已完成，待实机验收**）：读 `PhysicsModule+0x120` 线速度，模长 > 0.5 m/s 立即清空裂纹；
   用速度而不是位置差，浮动原点重定位不会误触发。核心测试见 `tests/test_ground_mining.cpp` 的三个移动用例。
   实机验收时确认：站立长按不被打断、WASD 移动立即掉裂纹、被打退/下滑同样打断。
7. **实机验收**：按 issue #81 的验收标准，需要在草地与矿洞各跑一次长按采掘，检查日志、裂纹、背包与退出码。

## 10. 关键代码入口

- 掉落表数据：`include/mc/loot_table.hpp` → `kDefaultLootTable`
- 掷骰：`mc::LootTable::rollBase` / `mc::LootTable::roll`
- 采掘状态机：`mc::GroundMiningController::update`（`moving` 入参 = 移动打断）
- 区域自动识别：`include/mc/zone_detector.hpp` → `decodeBlockId` / `classifyZone`；`include/mc/er_region_table.hpp`（map tile 表，生成器 `tools/reverse/gen_er_region_table.py`）；适配层 `PlayerBlockId`、`SetupZoneProbe`、`HitCollectorDetour`
- 适配层：`adapters/eldenring/src/loader.cpp` → `FindBlockTarget`、`StepMining`、`AimPitchDeg`
- 配置解析：`mc_er_steve.txt` 的 `ground_mining` / `loot_zone` / `zone_mat_*` / `zone_area_*`
- 探针签名：`adapters/eldenring/include/eldenring_sigscan.hpp` → `kRaycastHitCollector` / `kHavokMaterialLookup`
- 图集：`tools/extract_mc_assets.py` → `HUD_SPRITES` / `_JAR_HUD_SPRITES` / `_JAR_BLOCK_ITEMS`
