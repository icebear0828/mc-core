# 艾尔登法环 Minecraft 适配器：交接文档

给**新开的 Claude Code 对话**（或接手的人）。先读完这一份，先看紧接着的"0. 当前状态"，再按"第 11 节"的顺序动手。最后更新：2026-10-09 夜，分支见第 0 节。
需要查证据时，权威文档是 `docs/ELDENRING_REVERSE.md`（逆向事实与审计）和 `docs/ELDENRING_VERIFY_CHECKLIST.md`（逐项验证状态，F1~F22 是近战/生存/视觉/音效）。

---

## 0. 当前状态（2026-10-09 夜，最新，先读这里）

**分支与部署**
- `origin/main` = `a1d2017`（头部跟随相机，用户已实机确认）。本地 `main` 多一个 `a856f75`（只读召唤监视的第一版，未推送）。
- **召唤相关全部在 `feat/buddy-probe`（已推送，最新代码提交 `1431367`，文档 `9de3545`），未合入 main。** win 仓库 `D:\game\mc\mc-core` 停在 `feat/buddy-probe` @ `1431367`；游戏目录 `dinput8.dll` = 该提交的构建，SHA256 `80ce4cce658de9a3a9dbd21f925f35f811859663c345441569214b9c69f571fc`（两边一致）。
- **项目有 PreToolUse 钩子禁止直接 `git push` 到 main/master，必须走分支 + PR。**（`a1d2017` 那次是命令以 `cd` 开头没被钩子匹配到，无意绕过；以后别这么做。）
- 用户的打包计划：**按功能拆版本，创造模式（MC 起跳 + 飞行 + 免摔死）优先级最高**，其次召唤物、体感（光照）。每个功能开独立分支（`feat/creative`、`feat/summon`、`feat/feel`），在配置里单独开关，默认关，验证后再默认开。**这一轮 mac 测试 608 个全过（main 是 597）。**
- 游戏目录里现在有两个测试开关文件：`mc_er_buddylog.txt`（**存在时探针线程会占满一个 CPU 核**，不测时删掉）和 `mc_er_summon.txt`（存在时 F2 才生效）。仓库里有用户的未跟踪文件 `docs/123/youhua.md`、`package_specs/`，**不是我们写的，别动**。`docs/ELDENRING_HANDOFF.md` 之前的未提交改动（删了旧第 11 节"用户偏好"，因为已在全局 CLAUDE.md）一并提交了。

**2026-10-10 更新（`feat/creative`）**：已加 4 个只记录探针（击杀包装 `0x1403EDA70`、重着陆 `0x14044E090`、坠落高度 `0x14044E240`、SpEffect 查询 `0x1404FA370`，开关 `mc_er_creativelog.txt`）；修复“摔死后不能复活”（`fall_protect` 不再在 `hp<=0` 时跳过 `KillChr`，用户已实机确认）。**摔死走摔伤路径（`0x14044E240` → `0x411324` ×100），不走重着陆**，下一步免摔 = hook `0x14044E240` 对玩家返回 0，一步一测。**用 RTSS（RivaTuner）时带我们的 dll 会崩，玩 mod 时先退出 RTSS。** 核对结论见 `ELDENRING_REVERSE.md` §31。

**2026-10-10 晚更新（`feat/creative`，创造模式基本完成，用户已实机确认）**：F5 切生存（默认）/创造；创造 = 物品选取区 + 免摔 + 物品不消耗 + 无血/饥饿/经验条；双击空格飞行（`eldenring_flight.hpp`：自己积分位置、只写 `+0x70/+0x80/+0x91`，跟随游戏 8 m 坐标重基，Steve 朝向取相机朝向）；创造模式下玩家不会被"滞空 12 秒"/击杀包装杀死（HP>0）。**机制与证据见 `ELDENRING_REVERSE.md` §32。** 还没做：飞行地形碰撞（v0 能穿地面和墙）、MC 起跳、`creative_*` 配置项、探针收尾（`mc_er_creativelog.txt` 目前仍开着，日志较多）。win 构建现用 Windows SDK 10.0.19041.0。**玩 mod 时先退出 RTSS。**

**待逆向清单（已整理好，用户会丢给逆向 agent）：`docs/REVERSE_REQUESTS.md`**——创造模式 C1~C8、召唤 S1~S6、光照 L1~L2、战斗手感 H1~H4、弓箭、其它，含证据格式要求和已确认事实；回复回来后按第 8 节规则逐字节核对，结论写进 `ELDENRING_REVERSE.md` 新的一节。
**光照捕获那份回复（`d3d12_lighting_capture_hook.hpp`）暂不采信**（文件不在仓库、无真实抓取数据），见 REVERSE §30。

**这一轮完成并经用户实机确认**
1. **第三人称头部跟随相机**（`HeadTracker`，`eldenring_steve.hpp`）：偏航最多偏离身体 50°，俯仰全范围，相机在身体正后方时保持上一侧。已在 main。
2. **召唤物（灵灰）能由我们触发**（`feat/buddy-probe`）：F2 + `mc_er_summon.txt`，在游戏线程（`ClampDetour`）写 `CSBuddyMan`（`WorldChrMan(0x143D69FF8)+0x1E538`）：出生点 `+0xA0`（4 个 float）、朝向 `+0xB0`、石碑 `+0x3C`=1042360100，最后请求 `+0x20`=232000（群狼，ash 2320 × 100 + 等级 0）。三只狼（`npc=4070 team=47 hp=500`）出现在玩家面前 3 米、随朝向，开不开 MC 模式都行。出生点规律（A 级）：位置 = 玩家位置 + 3 m × `-(sin h, 0, cos h)`，朝向 = `yawFromQuat`；只写请求和石碑时狼会出生在残留的世界点然后一直下落（屏幕上只剩常驻血条）。详见 `ELDENRING_REVERSE.md` §29、§29.1。
3. **只读探针**（`eldenring_buddy.hpp` + `loader.cpp` 的 `BuddyThread`）：开关 `mc_er_buddylog.txt`，约 1 ms 轮询管理器，字段变化时写 `buddy:` 日志（含原始转储和玩家位姿）；F2 后 1.5 s、5 s 记录新出现的实体（`summon:   new chr=...`）。

**逆向方回复的核对结果（我们逐字节核对，别再采信被推翻的）**
- **推翻**：`0x14044E090` **不是**"落地复位"，**不能调用**：没有 SpEffect `0x8F` 时写 `[[chr+0x190]+8]+0x34=6`、调虚函数、并设全局标志 `+0x9328`；它只有 2 个调用者 `0x14045AF25/0x14045AF90`。
- **推翻**：`[[chr+0x58]+0xC8]+0x24` bit0 **是"已死亡"标志**，不是免死标志：`KillChr(0x1403FCD90)` 内 `0x1403FCDE1` 置位，复活初始化 `0x1403EA2C0` 清除（同时 `FallModule+0x1D=0`）。**绝对不能写它**。之前"跳过 KillChr 留半截死亡"的原因**不是**这个标志（我们跳过的正是置位它的函数），而是击杀包装 `0x1403EDA70` 里 KillChr 之后继续执行的死亡处理。
- **推翻**：`PhysModule+0x1C0`（setter `0x14045FB30`，3 个调用点 `0x14042687E/0x140428780/0x14042879D`）不是重力，是地面运动里算出来的标量；别写。`IsFlyState` 只是脚本函数名表里的字符串（`0x142A093D0`），不是飞行机制；`0x142EF5DE0` 是数据不是代码。
- **未采信**："每帧把 `FallModule+0x18` 写 0"（我们 5 种写法都失败，且无地址）；"立即起跳 ActionModule vfunc[18]"（无地址）。
- **召唤侧推翻**：逆向方的 BuddyParam/NpcParam ID（21200000 等）与实测不符；虚表 RVA 是 `+0x3B458B8`；`0x1404B82D0` 不是 DismissBuddy 序言；`ELDENRING_VERIFIED_EVIDENCE.md` 不存在。
- 另：`docs/ELDENRING_REVERSE.md` §29 记录了这些核对。

**下一步（用户睡前说"以后再说"，没有开始）**
1. **创造模式（最高优先级，开 `feat/creative`）**：思路是对玩家**跳过决策函数**，不往游戏状态字段写值。验证顺序，一步一测，每步只问用户"能走/不能走/死没死"：
   ① 装两个**只记录、不改行为**的钩子：击杀包装 `0x1403EDA70`（4 个调用者 `0x1403E93DE/0x1403F8543/0x140428EEB/0x14042BC1E`）和重着陆 `0x14044E090`，记录调用者地址、是否玩家。用户跳、摔、堆高柱子摔下，看是哪条路径在杀玩家。
   ② 日志确认后，仅在创造模式开启时对玩家跳过包装，测摔下去会不会死、死后状态是否正常。
   ③ 飞行本身：自己积分位置（沿用方块碰撞已验证的 `+0x70/+0x80` 位置写法），不动任何标志位。
   ④ 落地：再决定是否跳过 `0x14044E090`。
   MC 起跳另走二分：从 `d9103d0`（自写起跳弧线、不写坠落计时、无击杀钩子，用户说垒柱子顺）往后一个提交一个提交加，每步用配置开关隔离。
2. **召唤物（开 `feat/summon`，由现有 `feat/buddy-probe` 改名/合并而来）**：目标是 MC 的僵尸/骷髅/苦力怕。先让用户用不同的骨灰各自然召唤一次，探针记下请求值和 `npc id`，找单体骨灰；再识别 `team=47` 实体、隐藏它们的原模型（现有隐藏只作用于玩家）、用我们的渲染画 MC 模型；骨骼用 `include/mc/entity_model.hpp`（JSON 解析已存在但**没有任何适配器调用**，且缺骨骼矩阵、逐面 UV，见对话记录的缺口清单：`cubeFaces` 与 `rig.cpp::boxFaces` 重复、坐标系与 Steve 的 Y 轴方向不同、`uint16_t` 索引可能溢出）。
3. **体感/光照（开 `feat/feel`）**：现在 `PSMain` 只有 MC 固定面明暗，颜色直接写进显示空间的后缓冲（交换链格式 24 = R10G10B10A2），所以"像贴上去"。可用：半球环境光、距离雾（距离用 `1/SV_POSITION.w`）。**不可照搬**：ACES + 伽马（画面已是显示空间，会发灰）和写死的太阳/天空/雾数值（昼夜、天气、洞穴都会不对）。两条路：①屏幕采样（复制后缓冲，在人物周围取点估计亮度和色调，不需要逆向）；②逆向方给游戏的太阳方向/颜色/环境光/雾参数（需要 A 级运行时地址）。
4. 其余待办见第 9 节（弓箭、受击倒地、背包存档、30 分钟稳定性长跑等不变）。

**用户的工作方式（再强调）**：经常 Alt+Tab，失焦是正常的；一次只改一件事、用配置开关二分；只问"能走/不能走"；不要在没问的情况下往游戏内存写新字段；不要凭猜测下结论；改完文件后先贴验证命令和完整输出，不说"改好了"。

---

## 1. 这是什么、现在到哪了

把 Minecraft 的玩法叠到《艾尔登法环》（`eldenring.exe` **2.7.1.0**，离线，无 EAC）里，做成 `dinput8.dll` 代理加载器（DLL 注入，MinHook 钩子）。核心规则在平台无关的 `mc_core`（`src/`、`include/mc/`），ER 适配器在 `adapters/eldenring/`。

**已在真机上验证（用户确认）**：真实 Steve 皮肤和动画、隐藏原模型与影子、真实 HUD 图集（热键栏、红心、金心、物品图标）、近战（手持物品伤害、蓄力冷却、暴击）、命中/暴击/击杀标记、伤害心形/暴击星星/横扫粒子、受击红闪和第一人称镜头倾斜、金苹果右键进食（再生、吸收）、不死图腾、90 个原版音效、墙体遮挡、第一人称 view model（空手手臂、手持物品，原版矩阵链）。

**未做/被卡住**：见第 9 节。最大的缺口是**30 分钟稳定性长跑还没跑过**。

---

## 2. 环境与工作流（先看这个，踩过很多坑）

| 项 | 内容 |
|---|---|
| 开发机 | macOS，仓库 `/Users/c/mc-core`。**只在这里改代码和跑测试**（`cmake --build build -j8 && ./build/bin/mc_tests`）。ER 加载器的 `.cpp` 在 mac 上**不会被编译**（Windows 专用），所以 mac 全绿不代表 win 能编译。 |
| 目标机 | Windows，`ssh win`。仓库 `D:\game\mc\mc-core`（只此一份，别碰 `D:\game\mc-core`、`D:\game\sekiro`）。游戏目录 `C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game`。 |
| 代码传输 | **只走 git**（push → win 上 pull）。win 访问不了 GitHub，要用代理：`git -c http.proxy=http://127.0.0.1:7897 pull`；TLS 经常断，**脚本里循环重试直到 `git rev-parse --short HEAD` 两边一致**，别信"没报错"。资产类文件（皮肤、声音）例外，用 scp。 |
| 编译（win） | `cd /d D:\game\mc\mc-core && call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 > build-env.log 2>&1 && cmake --build build-win --config Release --target eldenring_adapter > build-er.log 2>&1 & findstr /C:"error" /C:"warning" /C:"eldenring_adapter.vcxproj ->" build-er.log`。MSVC 是 **`/W4 /WX`**（警告即错误）。输出是 GBK，管道过 `iconv -f GBK -t UTF-8 -c`。 |
| 部署 | 游戏**必须关着**，把 `build-win\bin\eldenring\dinput8.dll` 复制到游戏目录，再 `certutil -hashfile` 对比两边哈希。**游戏进程名是 `start_protected_game.exe`，不是 `eldenring.exe`**——用 `tasklist \| findstr /I "eldenring start_protected"` 判断是否在运行。DLL 被占用就是游戏（或启动器）还开着。 |
| 日志 | 游戏目录 `mc_er.log`，**追加写入，多次运行首尾相连，时间戳每次从 0 重新开始**。分析时按"时间戳变小"切会话，只看最后一段。 |
| Python | 一律 `uv run`（用户规定），例如 `uv run --with pillow --with pytest python -m pytest tests/test_extract_assets.py`。win 上：`uv run --python C:\Python313\python.exe --with pillow python ...`。 |
| 测试 | mac：`./build/bin/mc_tests`（C++，gtest）；Python：`tests/test_*.py`。改了 `tools/extract_mc_assets.py` 的精灵表后要同时更新 `include/mc/hud_atlas.hpp`、`adapters/sekiro/include/sekiro_hud_atlas.hpp`、`assets/source/textures/mc_hud_atlas.png`（有测试逐字节比对，**不要手改生成物**）。 |

**一次完整迭代的标准流程**：写测试 → 改代码 → mac 测试全绿 → `git commit` → push（失败就重试）→ win 同步到同一提交 → win 编译（无 error/warning）→ 确认游戏已关 → 部署并核对哈希 → 告诉用户测什么 → 用户反馈后拉日志核对。

---

## 3. 仓库地图（ER 相关）

`adapters/eldenring/include/`（**平台无关的纯逻辑，mac 上有测试**）：

| 文件 | 内容 |
|---|---|
| `eldenring_live.hpp` | 布局常量、`Vitals`、`readVitals`（`IMemoryReader` 抽象，测试用假内存） |
| `eldenring_rtti.hpp` / `eldenring_singletons.hpp` / `eldenring_sigscan.hpp` | RTTI 校验、单例读取、签名扫描（**恰好 1 处命中才绑定**）。**所有签名常量在 `sigs::`** |
| `eldenring_world.hpp` | 敌人枚举、物理位置/朝向/速度、`readAirborne`（`+0x92==0`）、阵营矩阵 |
| `eldenring_state.hpp` / `eldenring_camera.hpp` | 菜单/读盘/淡入状态、`ChrCam` 读取与投影、`firstPersonEye` |
| `eldenring_damage.hpp` | `DamageQueue`、`HitContext` 构造、`overrideFinalDamage`（改 `ctx+0x228`） |
| `eldenring_melee.hpp` | `MeleeController`（热键栏、数量、冷却）、`JumpTracker`、`HitFeedback`（命中/击杀/图腾/受伤计时）、`erDamage` |
| `eldenring_survival.hpp` | 再生、吸收、图腾夹伤、`scaleToEr`（MC 20 点血 ↔ 角色最大血量） |
| `eldenring_los.hpp` | 视线判定（射线，注入式，两端容差，失败放行） |
| `eldenring_steve.hpp` | Steve 朝向、`SteveMotion`、`HeadTracker`（第三人称头部跟随相机）、死亡倒下 |
| `eldenring_buddy.hpp` | （`feat/buddy-probe`）`CSBuddyMan` 只读采样、`Monitor` 日志、召唤出生点/写入计划、`newEntities` |
| `eldenring_fp.hpp` | 第一人称矩阵链（`itemPose`/`eatPose`/`bareArmPose`/`itemDisplay`/`walkBob`/`handSway`）、`HandAnimator`、`SwayFilter`、`toHost`（MC 右手系 → 渲染左手系） |
| `eldenring_particles.hpp` | 粒子系统、受击镜头倾斜曲线 |
| `eldenring_hudtex.hpp` / `eldenring_audio_data.hpp` | 物品→图集格、最近邻放大；声音清单/WAV 解析、脚步节拍 |
| `eldenring_model.hpp` / `eldenring_pick.hpp` | 玩家部件槽（隐藏原模型）、准星选目标 |

`adapters/eldenring/src/`（**Windows 专用，只在 win 上编译**）：`loader.cpp`（总入口、全部钩子、按键线程、HUD 数据）、`overlay_d3d12.cpp`（Present 钩子、ImGui、HUD、粒子、view model 调度）、`steve_renderer_d3d12.cpp`（Steve/手臂/物品 D3D12 渲染）、`input_hook.cpp`（DirectInput）、`audio_xaudio2.cpp`、`png_wic.cpp`。

核心库：`src/combat.cpp`（MC 伤害表、暴击、横扫）、`src/consumables.cpp`（进食）、`src/item_model.cpp`（`buildFlatItemMesh`）、`src/rig.cpp`（Steve 12 部件网格）、`src/hud_layout.cpp`（原版 GUI 缩放排布）。

工具：`tools/extract_mc_assets.py`（皮肤、HUD 图集）、`tools/extract_mc_sounds.py`（声音）、`tools/reverse/eldenring/`（只读探针脚本）、`tools/reverse/eldenring/templates/mc_er_hit.bin`（**受击模板备份**，见 5.2）。

---

## 4. 运行时架构

**线程**：①游戏线程（`g_game_tid`，`ClampHP` 钩子在这里跑）：改伤害、调 `SetHP`；②**按键线程** `KeyThread`（15 ms 轮询）：热键、点击、冷却、进食、再生、脚步；③**Present 线程**（`RenderFrame`）：HUD、Steve、粒子、view model；④音频线程。**规则：写游戏状态、调游戏函数一律在游戏线程**（回血先放进 `g_heal_pending`，由 `ClampDetour` 在游戏线程消化）。共享状态（`g_melee`、`g_feedback`、`g_survival`、`g_eating`）用 `g_melee_mutex`。**带 `__try` 的函数里不能有带析构的局部对象**（MSVC C2712）：把锁拆到独立函数。

**钩子表**（全部由签名扫描定位，不唯一就不装，失败放行）：

| 钩子 / 调用 | RVA | 常量 | 作用 |
|---|---|---|---|
| `SetMaxHPAndClampHP` | `0x438870` | `sigs::kClampHp` | 每个数据模块每帧一次；在这里排空伤害队列（`DamageQueue`）、消化回血 |
| `ProcessDamageContext` | `0x448910` | `sigs::kProcessDamageContext` | **最终伤害替换**（我们的命中，`ctx+0x228`）；玩家被打时的吸收和图腾夹伤；`HIT-IN` 日志 |
| 渲染相机拷贝 | `0x4A7190` | `sigs::kRenderCameraCopy` | 渲染前：第一人称改相机位置/受击倾斜（拷贝后还原）；清除原模型显示位 |
| 相机更新 | `0x3B11D0` | `sigs::kCameraStepExecute` | 签名 `(ChrCam*, float dt@xmm1, ChrIns*, bool)`。执行前还原引擎相机，执行后把眼睛位置写进 ChrCam+0x40 并保持到下一帧，使特效/粒子/声音等所有读相机处都用眼睛位置（`fp_persist=0` 关闭）。**由硬件写断点（`mc_er_camwatch.txt`，会卡死，别常开）在 `0x3B1929 movaps [rdi+0x40]` 抓到；`0x3BC070` 不是它（日志 changed=0），已证伪。** |
| 受击反应挑选器 | `0x1404547C0` / `0x1404548A0` | `sigs::kHitReactDefault/Heavy` | `no_stagger=1` 实验：玩家受害者时跳过（**默认关，需求不符：用户只要去掉倒地，普通受击反馈要保留**，见 REVERSE 21.4） |
| 输入总闸门 | `0x14067B020` | `sigs::kIsInputBlocked` | 背包打开时返回 1（6 个调用者）。**实测背包开着时从未被调用（forced=0），键盘屏蔽靠 DirectInput 层清零；保留但不是必需** |
| 相机转动冻结 | `0x140766C60` | `sigs::kMenuFreezesCamera` | 背包打开时返回 1。**实测有效（每秒 120 次）**。有一个只差 call 位移的孪生函数 `0x140766BC0`，签名带了字面位移才唯一。 |
| 受击特效生成（可选） | `0x450120` | `sigs::kHitVfxSpawn` | 放 `mc_er_hitvfx.txt` 才装；血液已用游戏设置关掉，通常不需要 |
| `ApplyHPChange`（调用，非钩子） | `0x437450` | `sigs::kApplyHpChange` | 回血，**游戏线程** |
| 射线包装（调用） | `0xC71D70` | `sigs::kRaycastWrapper` | 墙体遮挡，过滤器 `0x5D`，在 `DamageQueue` 里（游戏线程）调用 |
| 敌人伤害入口（虚表调用） | `vfunc[7]`=`0x4455C0` | — | 我们造成伤害：单次调用，引擎自己算硬直/音效/仇恨/死亡 |
| D3D12 | Present / ResizeBuffers / ExecuteCommandLists / CreateDepthStencilView | — | 覆盖层、命令队列捕获、深度缓冲识别 |
| DirectInput | `CreateDevice`、设备 `GetDeviceState/GetDeviceData` | — | 吞鼠标键、取左/右键边沿和滚轮 |

**伤害链（要点）**：点击 → `ClickAttack`（选目标、`MeleeController::makeIntent` 算 MC 伤害 → `erDamage` 换成最大血量百分比）→ `DamageQueue.enqueue`（`tag` 位：1=暴击、2=横扫、4=强击、位 8~15=MC 伤害整数）→ 游戏线程 `ClampDetour` 排空 → 视线检查（射线）→ 构造 `HitContext`（模板见 5.2）→ 调 `vfunc[7]` → 引擎走 `ProcessDamageContext`，我们的钩子把 `ctx+0x228` 换成目标伤害 → 结果回到 `DrainOnce`：命中标记/粒子/音效。

---

## 5. 游戏目录里的东西

### 5.1 开关文件（空文件即可，存在就启用）

`mc_er_overlay.txt`（覆盖层）、`mc_er_damage.txt`（伤害功能）、`mc_er_input.txt`（输入拦截）、`mc_er_hit.bin`（**必须**，见 5.2）、`mc_er_steve.txt`（配置，见下）、`mc_er_physlog.txt`（物理向量日志）、`mc_er_hitlog.txt`（`HIT-IN` 受击日志）、`mc_er_hitvfx.txt`（装受击特效钩子）、`mc_er_buddylog.txt`（召唤管理器只读探针，**会占满一个 CPU 核**）、`mc_er_summon.txt`（让 F2 触发一次群狼召唤）、`mc_er_nolos.txt`（**应急**：关闭墙体遮挡）、`mc_er_anyvictim.txt`（不等受害者被更新，有小概率与 worker 线程竞争）。

### 5.2 `mc_er_hit.bin`（576 字节，必备）

一次真实"玩家→敌人"命中的 `HitContext` 样本（逆向方采集）。伤害功能靠它构造上下文；缺了日志会写 `damage: mc_er_hit.bin missing`。**仓库里的备份：`tools/reverse/eldenring/templates/mc_er_hit.bin`**，装机时复制到游戏目录。其中 `+0x1D0/0x1D8/0x1E0` 是失效指针，每次构造都会覆盖，无害。

### 5.3 `mc_er_steve.txt` 配置键（`key=value`，一行一个）

`occlusion`（1=被场景遮挡）、`depth_const`（**0.00456**，深度×视距常数，实测）、`rel_bias`、`abs_bias`、`debug`/`depthview_gain`/`scene_height`（标定用，保持默认）、`yaw_offset_deg`（**180**，玩家物理四元数朝向与模型相反）、`hide_native`（1=隐藏原模型）、`hide_mask1`（`0x100A1`）、`hide_mask2`（`1`）、`hide_slots`（部件槽位掩码，默认全部）、`sound_volume`（0.8）、`first_person`（1=启动即第一人称）、`eye_height`（1.65）、`fp_persist`（1，持久眼睛相机）、`inv_key`（背包键的虚拟键码，默认 73=`I`）、`inv_sens`（背包指针灵敏度，默认 1）、`no_stagger`（0，实验，见钩子表）、`mc_jump`（0，起跳实验）、`mc_jump_key`（VK，默认空格 32）、`mc_jump_speed`（8.95 m/s）、`blocks`（1，放置/破坏）、`block_collision`（1，软碰撞）、`reach`（4.5 m）、`kb_force`（击退实验，**无效果，别用**）、`no_player_hit_vfx`（配合 `mc_er_hitvfx.txt`）。

### 5.4 热键（游戏窗口在前台时）

`F2` 召唤实验（需 `mc_er_summon.txt`）· `F6` MC 模式开关 · `F7` 切换深度候选（标定） · `F8` 打最近的敌人 · `F9` 逐槽隐藏部件（屏幕黄字显示槽号，`F6`/`F10` 会复位）· `F10` 第一人称 · `F11` 打印部件槽快照 · `F12` 记录 240 帧位置轨迹 · `I` 背包（Esc/再按 `I` 关；打开后：左键拿/放/合并/交换，右键拿一半/放一个，Shift+左键在热键栏和主背包间移动，数字键把悬停格与热键栏对换，物品栏面板上方是 17 种物品的创造选取区）· 滚轮/数字 `1~9` 切热键栏 · 右键 吃东西 · 左键 攻击 · 空格（写死）起跳判定。

---

## 6. 本地资产（**绝不入库**，资产政策见 `mojang-assets-policy` 记忆）

全部放在游戏目录 `mods\mc_adapter\`，缺失时加载器降级（平涂 / 矩形 HUD / 无声）并写日志。

| 文件 | 生成命令（win，仓库根） |
|---|---|
| `steve.png`（64×64）、`mc_hud_atlas.png`（**512×512**，40 个精灵，含 8 个新物品图标和容器面板） | `uv run --python C:\Python313\python.exe --with pillow python tools\extract_mc_assets.py --client-jar D:\game\sekiro\build\mc_client_1.21.8.jar --export-hud-atlas --export-steve-skin --out-dir "<游戏目录>\mods\mc_adapter"` |
| `sounds\`（90 个 WAV + `sounds_manifest.txt`） | mac 上：`uv run --with soundfile --with numpy python tools/extract_mc_sounds.py --out-dir <临时目录>`（从 Mojang 资源服务器下载，偶发 TLS 断线会自动重试），再 `scp -r` 到游戏目录 `mods\mc_adapter\` |

图集精灵表只往末尾追加（图集 2026-10-09 从 256 扩到 512，所有 UV 数值因此变了，新旧图集文件不能混用，换版本后必须重新生成）；改完要重新生成头文件和占位图集（见第 2 节）。

---

## 7. 已验证的实测事实（别再重新猜）

- **跳跃**：按空格后约 **0.72~0.81 s** 才离地；`PhysicsModule+0x92==0` 的窗口只有 **0.05~0.11 s**（跑跳/坠落更长）。所以暴击用 `JumpTracker`（按键到落地），不是"此刻在空中"。`+0x1D0` 不只是 1~2 帧；`+0x1D1` 不是干净的空中标志。
- **物理向量**：`+0x70` 当前位置；`+0x80` **上一帧位置**（差 = 速度/60）；`+0x120` **线速度 m/s**。
- **部件槽**（用户目测，B+）：0=头、1=裸体身体、2=头盔、3=胸甲、5=脚/内衬、6=头发、7=盾牌、11=武器、16/19/20=箭袋相关；4、21~25 无可见变化。**武器和盾牌在这些槽里**。藏头集合 `{0,2,6}` = `0x45`。
- **伤害**：引擎自己算的伤害与我们填的 `+0x228` 无关；真正生效靠 `ProcessDamageContext` 入口钩子替换（`override=applied`，`hp delta == wanted`）。
- **弹道受击**：`HitContext+0xDA == 6`、`+0x21C == -1`。
- **第一人称**：`0x1404A7190` 是渲染实际使用的相机拷贝（用户看到视角移动）。
- **输入**：游戏用 DirectInput `GetDeviceState` 读键鼠（60 Hz）；手柄走 XInput（我们**只支持键鼠**，用户决定）。
- **菜单标志**：`CSMenuMan+0x1C|+0x1D` 为菜单聚焦；`+0x1A` 是光标捕获，**不是**菜单标志。
- **血液特效、战斗时的原生 HUD**：都用游戏自带设置关掉。

---

## 8. 逆向方报告的审计规则与"已被证伪"清单

用户把另一个 agent（"逆向方"）的报告贴过来，我们要**核对字节再采信**：用 `ssh win 'C:\Python313\python.exe -'` 直接读 `eldenring.exe`（解析 PE 节，注意**有两个 `.text` 节**，调用者扫描要两个都扫）。报告写进 `ELDENRING_REVERSE.md` 时分级：A=实机读到，B=反汇编/单次样本，C=推断。**已被证伪或不可用，不要再采信**：

- `PhysicsModule+0x91` 是碰撞开关（它是 `chr_proxy_pos_update_requested`；引擎的"设置位置"函数写 `+0x70`、`+0x80` 和 `word [+0x90]=0x101`）；`+0x80` 是速度（不是）。
- `0x140436B18` 的 `call [r10+0x1E8]` 是盾牌判定（那里已经没有 `HitContext`）。真正的格挡参数是 `0x140448910` 的第 5 个参数。
- 槽位按"像素差"命名（睫毛 13 万像素等不可能）；`CSMenuMan+0x654C` 是 HUD 选项（读出 `0x3D240000`）；`0x140450120` 只为玩家运行（它是为所有命中运行）。
- `BulletSpawnData` 的 `+0x00/+0x08` 是指针（是 64 位句柄，默认 -1）；方块碰撞 `hknp*` 的"流程"（没有任何 RVA，不可调用）。
- 相机任务 `0x1403BC070` 是 ChrCam 的写入者（实测 changed=0；真正写入者是 `0x1403B11D0`）。
- `0x1404547C0/0x1404548A0` 是未格挡受击硬直的派发（只在**已格挡**分支）；`0x437A80` 是防御姿态（返回 `module0+0x138<=0`）；`0x448627` 写受击者生命值（写 `ctx+0x228` 最终伤害）；`BulletRequest` 大小 0xC0（≥0x101）。详见 REVERSE §20。
- `ELDENRING_VERIFIED_EVIDENCE.md`（引用过它，但不在本仓库）。

---

## 9. 待办（按优先级）

**P0（上线前必须）**
1. **30 分钟稳定性长跑**（含读盘、传送、死亡、反复开关 F6/F10、多次受击），检查无崩溃、无访问违例；钩子是否被 Arxan 还原（清单 C9、C10）。我们现在挂了 4 个游戏钩子 + D3D12 + DirectInput。

**P1（等逆向方，任务单都已写好）**
2. 玩家受击硬直移除、击退、无敌帧、盾牌格挡（`ELDENRING_REVERSE.md` §10）；相机类追问见 §11~§13，输入总闸门见 §12。
3. 弓箭：`spawn_bullet`（`0x1403A2CB0`）的 `BulletSpawnData`（`+0x00/+0x08` 是句柄，怎么由 `ChrIns` 得到）+ 我们自己的右键拉弓玩法（素材 `entity.arrow.*` 已提取，没接事件）。

**P1（不依赖逆向，我们自己做）**
4. ~~**背包界面**~~（**已完成并通过实测 2026-10-09**；剩：物品存档、手柄屏蔽、Esc 同时开游戏菜单的问题已被键盘清零解决）。原计划：：扩展 `extract_mc_assets.py` 导出创造模式背景/分页/更多物品和方块图标；`HudEngine` 扩到 36 格并合并 `MeleeController` 的热键栏；输入接管方案已定（`REVERSE §12`）：挂 `IsInputBlocked`（`0x14067B020`，签名 `48 8B 05 ?? ?? ?? ?? 0F B6 80 34 0C 00 00 C3`，**有 6 个调用者，其中 `0x140257E12`、`0x140AFED08` 含义未知**）和 `IsAnyBlockingMenuOpen`（`0x140766C60`，只有相机函数调用，100% 安全），背包打开时返回 1；虚拟光标用 DirectInput 的 `lX/lY`；开关键要避开 `E`（ER 的"互动"）和 `Esc`。
5. 手持方块的 3D 模型（现在是等距图标，扁平）；给 Steve 和 view model 加环境光；伤害数字（原版没有，没做）。
6. 玩家被毒/腐败/坠落致死时图腾救不了（它们不走 `ProcessDamageContext`）；脚步材质固定草地。

**方块（阶段 A 已做，待实测）**：`eldenring_blocks.hpp`（网格、射线、放置落点、软碰撞）、`eldenring_blockmesh.hpp`、渲染在 `SteveRenderer::drawBlocks`；真实 Havok 碰撞（阶段 B）见 REVERSE 22。
**【重要，2026-10-09 教训】站在方块上的移动：`mc_jump=0` + `fall_reset=0`（即 `c982998` 的行为）能正常走；`mc_jump=1`（我们自己的起跳，写位置 + 写 `+0x92/+0x1D0/+0x1D1` 标志）和往坠落模块 `+0x18`、物理模块 `+0x1B8` 写值的各种写法（写 0、常数 1.0、只在落地写一次、超限拉回），无一例外会让玩家走不动/爬行/镜头转速变慢。不要再靠"往游戏状态字段里写值"补坑；要做 MC 起跳，应当调用游戏自己的落地/复位函数（候选 `0x14044E090`，调用者 `0x14045AF25/0x14045AF90`，尚未核参数）。配置里这两项默认保持 0。
**MC 移动模型（用户 2026-10-09 提出，创造模式时一并做）**：现在跳跃是游戏原生的——有约 0.8 s 前摇，高度不够，所以做不到 MC 的"跳起来往脚下放方块"（垒柱子，需要起跳高度约 1.25 m、按下即起跳）；创造模式还要飞行（双击空格、上升/下降、不受重力）。计划：在方块上已经做通的办法（游戏线程每帧写 `PhysicsModule+0x70/+0x80` 位置、`+0x120` 速度、`+0x92/+0x1D1` 着地标志，见钩子表"相机更新"行与 `BlocksCollisionStep`）扩展成一层我们自己的"MC 运动层"：自己积分重力、起跳（按下立即给初速度 ≈ 8.4 m/s，MC 跳高 1.25 m）、飞行；需要弄清：原生跳前摇能否取消（输入门/动画事件）、写位置在非方块处是否同样有效、落地伤害（`ProcessDamageContext` 外的坠落伤害入口，见逆向待办 6）、与翻滚的关系。
**已知问题（暂搁置，用户 2026-10-09 认为不重要）——堆得很高（约 y≥13、离真实地面 6 m 以上）后的"坠落死亡"**：游戏用 `KillChr`（`0x1403FCD90`，调用者之一 `0x1403EDA9C`，在函数 `0x1403EDA6A` 附近，先 `call 0x1403FCD90` 再按 `[rbx+0x68]`（3 或 10 则跳过）做后续死亡处理）把 HP 设 0。`fall_protect` 把这一次 kill 跳过，但死亡流程的其它部分仍在走：之后会一直冒粒子、角色无法移动、F6 切换后看不到人也无法操作（只有我们的跳跃和放方块还能用）。别堆这么高；真要根治需要让游戏把方块当地面（Havok，阶段 B）或弄清死亡流程的其它状态位。日志：`kill: the game kills the player (caller 0x3EDAA1): SKIPPED`。
**HUD 还原到原版 MC（用户 2026-10-09 提出，不急，后做）**：扣血/回血的原版动画（心形受伤闪烁、回血时心跳动/闪白、低血量抖动）、饥饿条（现在 HUD 只有心，没有饥饿格）、经验条与等级数字、护甲条、氧气泡。素材走 `tools/extract_mc_assets.py` 追加精灵（`gui/sprites/hud/...`），逻辑放纯函数写测试。
**不做**：Havok 方块碰撞（阶段 B，逆向方自己降级为高风险）、敌人受击变红、手柄支持。

---

## 10. 踩过的坑（节省时间）

- **hook 拦截**：Bash 命令里出现 `sed -n` 之类的 `-n` 且以 `git commit/push` 开头，会被"禁止 --no-verify"的 PreToolUse 钩子**整条误杀**（命令根本没执行，连前面的 python 编辑都没发生）。把 `git commit` 放进单独的命令。
- **BSD sed（mac）**：`sed -i` 必须写成 `sed -i ''`；`grep` 的 `\|` 要用 `-E`。
- **cmd**：`echo x=1>> file` 会被当成句柄重定向（`1>>`），写文件用 PowerShell；`set G=...` 在 `ssh win '...'` 里不生效，写全路径。
- **MSVC**：`/W4 /WX`，变量遮蔽（C4456）、`for (uint16_t i : {0,1,2})`（C4244）、`__try` 与析构（C2712）都会炸；`overlay_d3d12.cpp` 里 `Logf(const char*)` **不是 printf 风格**，先 `snprintf` 再传。
- **ImGui DX12 后端是线性采样器**，改不了；所以图集放大 4 倍最近邻上传。
- 平涂时看不出的问题（面互相覆盖）贴图后会暴露：Steve 渲染器需要自己的深度缓冲（已有）。
- 不要信"mock/日志绿就算好"：用户要求涉及外部服务的改动用真实服务验证（这里是真实游戏，由用户操作，我们看日志）。
- 日志里 `click dropped`、`swing:`、`los:`、`DAMAGE:`、`SURVIVAL:`、`HIT-IN:`、`ground:`、`phys:`、`slots:`、`audio:` 都是诊断用的，出问题先 `findstr` 这些。

---


---

## 11. 新对话的第一步

1. 读：本文件 → `docs/ELDENRING_VERIFY_CHECKLIST.md`（尤其 F 节）→ 需要时查 `docs/ELDENRING_REVERSE.md` 对应章节（目录：§1 已确认、§3 受击管线、§8~§18 近期审计与任务单）。
2. 检查状态：`git status -sb && git log --oneline -5`；`ssh win 'cd /d D:\game\mc\mc-core && git rev-parse --short HEAD'`；`ssh win 'tasklist | findstr /I "eldenring start_protected"'`；`ssh win 'certutil -hashfile "<游戏目录>\dinput8.dll" SHA256'` 与 win 构建产物对比。
3. 跑基线：`cmake --build build -j8 && ./build/bin/mc_tests`（main 当前 597 个、`feat/buddy-probe` 608 个测试应全过）。
4. 问用户这次想做什么，**默认建议先做 P0（稳定性长跑）**，同时可以开始 P1 的背包界面素材。
5. 每个功能结束时：更新清单（F 节新增条目）、项目记忆（`~/.claude/projects/-Users-c-mc-core/memory/`）、如果有新逆向结论写进 `ELDENRING_REVERSE.md` 并分级。
