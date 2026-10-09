# 艾尔登法环 Minecraft 适配器：交接文档

给**新开的 Claude Code 对话**（或接手的人）。先读完这一份，再按"第 12 节"的顺序动手。最后更新：2026-10-09，分支 `feat/eldenring-m1`。
需要查证据时，权威文档是 `docs/ELDENRING_REVERSE.md`（逆向事实与审计）和 `docs/ELDENRING_VERIFY_CHECKLIST.md`（逐项验证状态，F1~F22 是近战/生存/视觉/音效）。

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
| `eldenring_steve.hpp` | Steve 朝向、`SteveMotion`、死亡倒下 |
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

`mc_er_overlay.txt`（覆盖层）、`mc_er_damage.txt`（伤害功能）、`mc_er_input.txt`（输入拦截）、`mc_er_hit.bin`（**必须**，见 5.2）、`mc_er_steve.txt`（配置，见下）、`mc_er_physlog.txt`（物理向量日志）、`mc_er_hitlog.txt`（`HIT-IN` 受击日志）、`mc_er_hitvfx.txt`（装受击特效钩子）、`mc_er_nolos.txt`（**应急**：关闭墙体遮挡）、`mc_er_anyvictim.txt`（不等受害者被更新，有小概率与 worker 线程竞争）。

### 5.2 `mc_er_hit.bin`（576 字节，必备）

一次真实"玩家→敌人"命中的 `HitContext` 样本（逆向方采集）。伤害功能靠它构造上下文；缺了日志会写 `damage: mc_er_hit.bin missing`。**仓库里的备份：`tools/reverse/eldenring/templates/mc_er_hit.bin`**，装机时复制到游戏目录。其中 `+0x1D0/0x1D8/0x1E0` 是失效指针，每次构造都会覆盖，无害。

### 5.3 `mc_er_steve.txt` 配置键（`key=value`，一行一个）

`occlusion`（1=被场景遮挡）、`depth_const`（**0.00456**，深度×视距常数，实测）、`rel_bias`、`abs_bias`、`debug`/`depthview_gain`/`scene_height`（标定用，保持默认）、`yaw_offset_deg`（**180**，玩家物理四元数朝向与模型相反）、`hide_native`（1=隐藏原模型）、`hide_mask1`（`0x100A1`）、`hide_mask2`（`1`）、`hide_slots`（部件槽位掩码，默认全部）、`sound_volume`（0.8）、`first_person`（1=启动即第一人称）、`eye_height`（1.65）、`fp_persist`（1，持久眼睛相机）、`inv_key`（背包键的虚拟键码，默认 73=`I`）、`inv_sens`（背包指针灵敏度，默认 1）、`no_stagger`（0，实验，见钩子表）、`blocks`（1，放置/破坏）、`block_collision`（1，软碰撞）、`reach`（4.5 m）、`kb_force`（击退实验，**无效果，别用**）、`no_player_hit_vfx`（配合 `mc_er_hitvfx.txt`）。

### 5.4 热键（游戏窗口在前台时）

`F6` MC 模式开关 · `F7` 切换深度候选（标定） · `F8` 打最近的敌人 · `F9` 逐槽隐藏部件（屏幕黄字显示槽号，`F6`/`F10` 会复位）· `F10` 第一人称 · `F11` 打印部件槽快照 · `F12` 记录 240 帧位置轨迹 · `I` 背包（Esc/再按 `I` 关；打开后：左键拿/放/合并/交换，右键拿一半/放一个，Shift+左键在热键栏和主背包间移动，数字键把悬停格与热键栏对换，物品栏面板上方是 17 种物品的创造选取区）· 滚轮/数字 `1~9` 切热键栏 · 右键 吃东西 · 左键 攻击 · 空格（写死）起跳判定。

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

## 11. 用户偏好（来自全局 CLAUDE.md，必须遵守）

中文交流、代码/commit 英文；简洁直接。**改完文件后不得说"改好了"，必须先贴验证命令和完整输出**；先 trace 根因再改，不猜；"之前能跑现在不能"先看 `git diff`；涉及代理路由/网络出口的改动**先问用户**；TypeScript 禁 `any`、Python 用 `uv run`；TDD（新增功能先写测试）；提交格式 `<type>: <description>`（feat/fix/refactor/docs/test/chore/perf/ci）。**这个项目里**用户明确说过"不用验证了，相信我就好了"的功能（Steve 皮肤等视觉项）不再要求 ≥3 次实机重复，但仍要登记结果。真实资产绝不提交。

---

## 12. 新对话的第一步

1. 读：本文件 → `docs/ELDENRING_VERIFY_CHECKLIST.md`（尤其 F 节）→ 需要时查 `docs/ELDENRING_REVERSE.md` 对应章节（目录：§1 已确认、§3 受击管线、§8~§18 近期审计与任务单）。
2. 检查状态：`git status -sb && git log --oneline -5`；`ssh win 'cd /d D:\game\mc\mc-core && git rev-parse --short HEAD'`；`ssh win 'tasklist | findstr /I "eldenring start_protected"'`；`ssh win 'certutil -hashfile "<游戏目录>\dinput8.dll" SHA256'` 与 win 构建产物对比。
3. 跑基线：`cmake --build build -j8 && ./build/bin/mc_tests`（当前 487 个测试应全过）。
4. 问用户这次想做什么，**默认建议先做 P0（稳定性长跑）**，同时可以开始 P1 的背包界面素材。
5. 每个功能结束时：更新清单（F 节新增条目）、项目记忆（`~/.claude/projects/-Users-c-mc-core/memory/`）、如果有新逆向结论写进 `ELDENRING_REVERSE.md` 并分级。
