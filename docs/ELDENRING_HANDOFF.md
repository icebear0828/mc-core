# 艾尔登法环 Minecraft 适配器：交接文档

给**新开的 Claude Code 对话**（或接手的人）。先读完这一份，先看紧接着的"0. 当前状态"，再按"第 11 节"的顺序动手。最后更新：2026-10-10 深夜（阴影、弩箭、命中伤害已合入 main），分支见第 0 节。
需要查证据时，权威文档是 `docs/ELDENRING_REVERSE.md`（逆向事实与审计）和 `docs/ELDENRING_VERIFY_CHECKLIST.md`（逐项验证状态，F1~F22 是近战/生存/视觉/音效）。

---

## 0. 当前状态（2026-10-10 深夜，阴影 + 弩箭 + 僵尸召唤已合入 main，最新，先读这里）

**分支与部署**
- **`origin/main`** 现在含：创造模式（PR #4）、生存站方块假死修复（PR #5），以及本次一个 PR 合入的 `feat/bullet-log`（它是从 `feat/summon` 和 `feat/shadow` 一路叠上来的）：**僵尸召唤**、**脚下阴影**、**弩箭发射与命中伤害**。三者都经用户实机确认。合并走 PR：`gh pr create --base main --head <分支>`，`gh pr merge N --merge`（GitHub 的 TLS 常断，循环重试并核对 `git rev-parse` 两边一致）。**项目有 PreToolUse 钩子禁止直接 `git push` 到 main/master，必须走分支 + PR。**
- `feat/mc-jump`（远端，未合并，**搁置**）。逆向方在 win 上的 `feat/lighting-root-cbv-probe`（他们的光照探针，**别合并**，见第 8 节）。
- 测试：mac `./build/bin/mc_tests` **715 个全过**；Python `uv run --with pillow --with pytest python -m pytest tests -q` **74 个全过**。win 编译 `/W4 /WX` 无 error 无 warning。
- **win 上有两份工作树，别弄混**：①`D:\game\mc\mc-core` 被逆向方切到 `feat/lighting-root-cbv-probe`（他们的探针，**不要动、不要往里 pull**）；②**我方构建用独立 worktree `D:\game\mc\mc-core-bullet`**（`git worktree add`，detached，`git checkout --detach origin/<分支>` 后构建）。它的 `build-win` 是单独配置的，依赖源码复用主工作树的缓存、离线：`cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_VERSION=10.0.19041.0 -DFETCHCONTENT_SOURCE_DIR_MINHOOK=D:/game/mc/mc-core/build-win/_deps/minhook-src -DFETCHCONTENT_SOURCE_DIR_IMGUI=D:/game/mc/mc-core/build-win/_deps/imgui-src -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=D:/game/mc/mc-core/build-win/_deps/googletest-src -DFETCHCONTENT_FULLY_DISCONNECTED=ON`。
- **游戏目录里的 `dinput8.dll` 是谁的？** 逆向方会把它换成他们的探针构建。我方构建产物是 `D:\game\mc\mc-core-bullet\build-win\bin\eldenring\dinput8.dll`，上一次部署的哈希 `b788b5c1163060597211c5bbb360c724a6a7a6986d0a33151b89a74d5988fd62`。备份：`dinput8.dll.bak_pre_lightcap`（我方、只含阴影，`3c5e812d…`）、`dinput8.dll.bak_reverser_probe`（逆向方，`f4dae1d6…`）。**部署前先 `certutil` 看当前是谁的；用 `cmd` 的 `copy` 部署**；shell 里 `echo "x =$(...)"` 在 zsh 会把 `=word` 展开，打印哈希时标签后面别紧跟 `=`。
- **win 构建环境**：Visual Studio 2022 Community 重装过，Windows SDK **10.0.19041.0**、MSVC 19.44（命令不变，见第 2 节）。
- **玩 mod 前先退出 RTSS（RivaTuner）**：它和我们的 `Present` 钩子冲突，带我们的 dll 时主菜单随机崩溃（`exe+0x3055359`、`d3d11.dll+0xad36e`，退出 RTSS 就好；见记忆 `er-rtss-crash`）。
- 仓库里有用户的未跟踪/未提交文件 `docs/123/`（逆向方回复），**不是我们写的，别动，提交时别 `git add -A`**。

**游戏目录里的开关文件**（`C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game`）：`mc_er_overlay.txt`、`mc_er_damage.txt`、`mc_er_input.txt`、`mc_er_hit.bin`（必备）、`mc_er_steve.txt`（配置，**保持 `mc_jump=0`、`fall_reset=0`**；原文件备份 `mc_er_steve.txt.bak_before_mcjump`）、`mc_er_summon.txt`（**F2 和僵尸刷怪蛋都要它**）、`mc_er_summonhide.txt`（隐藏召唤物原模型并画 MC 僵尸）、`mc_er_creativelog.txt`（只读探针，日志多，稳定后可删）。`mc_er_chrscan.txt`（召唤物结构扫描）和 `mc_er_buddylog.txt`（占满一个 CPU 核）不用时删掉。**弩箭相关**：`mc_er_bulletlog.txt`（只读记录 `spawn_bullet` 请求体，前 40 次，并开启 `PROJ-HIT`/`PDC-STATS` 命中日志）、`mc_er_bulletfire.txt`（F3 发射一支弩箭）、`mc_er_arrowdmg.txt`（我们的弩箭命中伤害换成 MC 伤害）、`mc_er_bullet.bin`（请求体模板，272 字节，第一次真射击自动生成，**不是开关文件，别删**，删了要重新真射一发）。`mc_er_lightcap.txt` 是逆向方探针的开关，我们的 dll 不读它。

**已完成并经用户实机确认**
1. **创造 / 生存模式（F5，MC 模式内，默认生存）**：创造 = 背包物品选取区 + 摔落伤害清零 + 物品不消耗 + 无血/饥饿/经验条；左上角显示模式名。逻辑集中在 `eldenring_creative.hpp`。
2. **创造飞行（双击空格）**：`eldenring_flight.hpp`。自己积分位置，只写 `PhysicsModule +0x70/+0x80` 和同步请求字节 `+0x91`（游戏自己的"设置位置"也写它，`0x14045C910` 消费并清零，用户同意）；游戏每隔一段把物理坐标整体平移 8 m 的整数倍（浮动原点），`followRebase` 跟随；飞行时 W/A/S/D/空格/Shift 对游戏隐藏（DirectInput）；Steve 朝向取相机朝向。**飞行没有地形碰撞（能穿地面和墙）。**
3. **创造模式不会死**（HP>0 时）：`0x14044E240` 摔落高度对玩家返回 0；击杀包装 `0x1403EDA70` 整个跳过；`0x14044E3A0`"滞空是否超限"返回否（**真正的原因**：连续滞空 `FallModule+0x18` 到 12.00 s 会排队一个"处死"事件，与高度无关）。生存模式同样在"站在放置的方块上/我们的移动逻辑 3 s 内"时回答否。HP=0 时一律放行，真死亡可复活。证据见 `ELDENRING_REVERSE.md` §31、§32。
   - **已知限制（用户 2026-10-10 实机发现，暂不处理）：创造模式不是无敌的。** 上面三个钩子只挡"摔落/滞空处死"，不挡战斗伤害：创造模式下被打仍会掉血、HP 到 0 仍会死（HP=0 时钩子一律放行）。MC 创造模式应当无敌，所以这是与 MC 行为的差距，不是回归。要做的话：在 `ProcessDamageContext`（`0x448910`）的玩家受害者分支里，创造模式下把最终伤害 `ctx+0x228` 置 0（沿用 `overrideFinalDamage`），不要往血量字段写值；虚空/`/kill` 类的真死亡仍应放行。
4. **召唤物（僵尸）**——已合入 main（原 `feat/summon`），流程见下。

**召唤物管线（已合入 main）**
- 触发：F2，或**创造模式选取区里的"僵尸刷怪蛋"拿在手里右键**（生存模式用掉一个；3 s 冷却）。都走 `QueueSummon` → 游戏线程写 `CSBuddyMan`（出生点 `+0xA0`、朝向 `+0xB0`、石碑 `+0x3C`、请求 `+0x20`=232000，群狼 `npc 4070 team 47 hp 500`，**一次三只**）。
- 识别：每帧 `enumerateEnemies(..., include_dead=true)` 取 `team 47`。**必须包含已死的**：不包含时被一击打死的召唤物直接从列表消失，永远读不到 `hp=0`（这是死亡动画一度不触发的原因）。
- 隐藏原模型：狼的结构是 `chr+0x50 → CSChrModelIns → +0x18/+0x188 → CSModelDispEntity`，和玩家部件是同一个类，`+0x20` 是显示标志（狼 `0xA7`，玩家 `0x100A1`，最低位=正在绘制）。每帧渲染前清最低位（用户同意只清这一位）；MC 模式关掉时还原；按 RTTI 类名动态收集，不写死偏移。
- 绘制：`eldenring_mobs.hpp` 的 `MobRegistry`（每只一套走路动画；生命值下降红闪 0.5 s；归零则倒地、保持红色、**只画 1 s**，游戏会把尸体留约 7.6 s）。僵尸由 **Bedrock geometry JSON** 驱动（`assets/source/models/entities/zombie.geo.json`，`mc::model::EntityModel`）：骨骼 pivot、默认旋转（手臂 `[-90,0,0]`）、父子链、`mirror`（左臂左腿镜像右边的 UV）。`SteveRenderer::drawMobModel` 一骨骼一网格一矩阵；JSON 读不到则退回 Steve 骨架。
- 资产（**只放游戏目录，绝不入库**）`mods\mc_adapter\`：`steve.png`、`zombie.png`、`mc_hud_atlas.png`（含刷怪蛋图标；**加精灵后要重新生成**）、`models\*.geo.json`。生成命令：`uv run --python C:\Python313\python.exe --with pillow python tools\extract_mc_assets.py --client-jar D:\game\sekiro\build\mc_client_1.21.8.jar --export-mob-skin zombie --export-hud-atlas --out-dir "<游戏目录>\mods\mc_adapter"`；JSON：`--export-entity-models --out-dir "<…>\models"`。1.21.8 的 jar 里每种怪有自己上好色的蛋 `item/zombie_spawn_egg.png`。
- 已修的坑：①`mirror` 立方体以前只换了西/东面的 UV 偏移，没翻转每个面的贴图；②僵尸贴图是老布局（左臂左腿块全透明），所以改成 JSON 的 `mirror`，提取脚本也会把空的左肢块用镜像右肢补上；③召唤物的 `gone` 日志都带着正血量=被一击打死。

**未解决 / 待办（按用户的话）**
1. **召唤和死亡的粒子特效仍是游戏原版的白烟**（用户："未来再说"）。入口未知，需要只读探查，类似之前的受击特效钩子 `0x450120`（`kHitVfxSpawn`）；MC 风格的烟雾粒子还要在我们的粒子系统里加新精灵。
2. **碰撞/受击体积还是狼的大小**（用户："先接受"）。僵尸 1.95 m，狼约 0.9 m，所以打脚边才命中。办法：找人形体型的单体骨灰（黑刀蒂希、仿身泪滴…，让用户用不同骨灰各召唤一次，看 `npc id`/体型），或读改胶囊尺寸（风险大）。
3. 骷髅（人形，`skeleton.geo.json` 已在，可直接用同一管线）和苦力怕（四条腿，需要新的走路动画）；召唤物的行为（现在是狼的 AI）。
4. **站在放置的方块上走不动**（老问题，与起跳无关）：日志按着方向键 `feet` 的 x/z 几乎不动。怀疑游戏的角色控制器在 Havok 代理处移动，而我们每帧把 `+0x70` 推回方块顶；可以试在方块逻辑里也用 `+0x91` 同步代理（**写新字段前先问用户**）。
5. **MC 起跳搁置**（用户决定不做）：试过 v2（空格对游戏隐藏、只写位置、按 MC 逐 tick 数值，`peak 1.25 m / 0.55 s`，起跳本身没问题），方案在 `feat/mc-jump`。
6. 飞行地形碰撞、体感/光照（见下）、30 分钟稳定性长跑（第 9 节 P0）。
7. **创造模式不是无敌的**（用户 2026-10-10 实机发现，暂不处理）：现有三个钩子只挡摔落/滞空处死，不挡战斗伤害，HP 到 0 仍会死。要做的话在 `ProcessDamageContext` 的玩家受害者分支里，创造模式下把 `ctx+0x228` 置 0，不写血量字段。
8. **盾牌 / 副手 / MC 双持 / 盔甲**（用户 2026-10-10 提出，下一批）：盾牌判定可以不靠逆向——`ProcessDamageContext` 钩子里已经在改玩家受到的伤害（吸收、图腾夹伤），按攻击者位置和玩家朝向判正面 100°，挡住就 `ctx+0x228=0` 并放 MC 盾牌音；缺的是①被挡时不要受击硬直和击退（硬直决策点逆向方至今没给出能用的，§21.4）、②盾牌外观（从 client jar 提取模型和贴图，只放游戏目录）、③副手格和右键举盾（右键现在是吃东西，要和副手栏一起设计）。盔甲：隐藏原生盔甲槽（槽 2/3/5），在 Steve 上画 MC 盔甲层，素材同样本地提取。建议这几项放在一起设计。

**脚下接触阴影（已合入 main，用户实机确认）**：`eldenring_shadow.hpp`（地面高度=向下射线与方块顶面取高、按离地高度衰减、单位圆盘）；`SteveRenderer::drawShadows` 画玩家和召唤物脚下的黑色柔边圆片（`PSShadow`，被场景深度和方块遮挡）；射线在游戏线程 `ShadowGroundStep`，配置 `shadow=0` 关闭。坑：管线描述复制自 `pd` 时 `pd.VS` 指向已释放的 blob，创建失败，必须自己再编译一份 VS。

**弩箭：发射 + 命中伤害（已合入 main，用户实机确认）**。F3 发射一支真实的游戏弩箭，命中后伤害换成 MC 的。证据与反汇编见 `ELDENRING_REVERSE.md` §35~§35.6，**逆向方的弩箭表（`20007000` 等、`+0x0C` 是 ID、`+0x08` 是发射者/目标句柄）全部不对，别用**。要点：
- `spawn_bullet`（`0x1403A2CB0`，`sigs::kSpawnBullet`）：`(CSBulletManager* [0x143D667A8], uint32_t* out_handle, request*, r9)`。**`r9` 只是游戏写状态码（0=成功）的指针，零缓冲就行。**请求体 0x110 字节：`+0x00` 发射者句柄（玩家 `ChrIns+0x08`）、**`+0x08` 是参数表的行 ID（不是目标句柄，-1/0/玩家句柄会被拒绝）**、`+0x1C` 子弹 ID、`+0x44` 标志（位 3 必须置位）、`+0x50` 行优先矩阵（right/up/forward/position）、`+0xB0` 置 0 安全。弩 + 普通弩箭：行 ID `0x06455A50`、子弹 ID **56**（跨会话不变）。
- 模板：第一次真射一发时，`SpawnBulletDetour` 把请求体存成游戏目录的 `mc_er_bullet.bin`，之后每次启动读取，F3 就不需要先射一发、也不需要手持弩。换弹药（行 ID 或子弹 ID 变）才会重写。纯逻辑在 `eldenring_bullet.hpp`（`buildFireRequest`、`templateUsable`、`boltDamageEr` 等），有测试。
- 发射在游戏线程（`ClampDetour`）里调用，带 SEH（`SpawnBulletSafe`）。F3 → `QueueBulletFire` → `g_bullet_fire_pending` → `RunBulletFire`。
- 命中：弩箭击中敌人时 `ProcessDamageContext` 看到 **`u8[DA]==2`、`attacker` 参数是玩家、`ctx+0x1D8` 是子弹对象**（`6` 只是玩家被箭射时的值）。`HandleProjectileHit` 在 `mc_er_arrowdmg.txt` 存在、最近一次 `spawn_bullet` 是弩箭（ID 56）且在 4 s 内时，把 `ctx+0x228` 换成 `boltDamageEr`（MC 弩箭 9 点，按敌人最大生命值的 5% × 9/7 折算，与近战同口径）。实测 `max_hp=219` 的敌人每发 14，F3 与真射一致（引擎原来算 13~25 / 74）。
- **MC 右键拉弓**（`feat/bow`，用户实机确认，`eldenring_bow.hpp`）：按住右键拉弓，松手发射，力度/伤害按 MC 规则缩放，生存模式消耗背包里的箭（起始背包主栏第 1 格有 64 支）。见 `ELDENRING_REVERSE.md` §35.7。
- **瞄准偏移（未解决，在查）**：重放请求发出的箭飞行方向偏离相机前向 25~42°（§35.9、§35.10）；偏航补偿无效已默认关闭（`bolt_yaw_offset_deg=0`）。原版真射不偏（请求前向≈相机前向，飞行≈请求）。现假设是标志位 `+0x44` 位 0（真射 `0x09`，我们曾发 `0x08`）决定按矩阵还是按玩家身体朝向，下一版已改成沿用模板标志，等 `AIMCAL` 复核。**第一人称拉弓姿势**已做（`bowPose`）；弓拉开时的带箭贴图 `bow_pulling_0/1/2`（MC 阈值 >0、≥0.65、≥0.9）已做（`ItemId::BowPulling0..2` 追加在末尾，图集追加三格，**游戏目录的图集 `mods\\mc_adapter\\mc_hud_atlas.png` 要用新工具重新生成**）。弩箭命中反馈（`entity.arrow.hit` 音、命中标记、伤害心形和暴击星）已接在 `HandleProjectileHit`。
- **弩箭外观不做 MC 覆盖**（用户看到游戏弩箭与 MC 箭差不多）。子弹实时位置已查清但不使用：`[manager+0x0]` → `CSBulletIns`，`+0x10` 位置、`+0x20` 四元数；原生弩箭是特效（`GXFfxSceneCtrl`），没法隐藏；见 §35.8。位置探针、堆扫描、差分快照、挂点实验都已删除（结论在 §35.8、§35.11）；只保留一个精简的 `AIMCAL`（开关文件 `mc_er_aimcal.txt`，只读，默认没有）：射击后 0.5~1 s 读该子弹对象，把飞行方向和我们给的瞄准、相机、玩家身体偏航对比，用来验证方向修好没有。
- **没做**：第一人称拉弓动画和弓的三阶段拉伸贴图；箭速随力度变化；击退、命中音效/标记；其他弹药和弓（每种要真射一发学它的行 ID 和子弹 ID，用户没有弓）。

**体感/光照（阴影之后的下一步；未开始写渲染侧）**：现在 `PSMain` 只有 MC 固定面明暗，颜色直接写进显示空间的后缓冲（交换链格式 24 = R10G10B10A2）。可用：半球环境光、距离雾。**不可照搬**：ACES + 伽马和写死的太阳/天空/雾数值。两条路：①屏幕采样（不依赖逆向，要在画角色前拷一份后缓冲）；②读游戏真实光照。**②的现状（`ELDENRING_REVERSE.md` §34）**：逆向方的同进程昼夜差分显示，`SetGraphicsRootConstantBufferView` root 1（调用点返回地址 `0xECBCF0`，绑定函数 `0xECBCC1` 起，分配器 `0xECC2B0`）里堆内偏移 `+0xC00` 的 256 字节缓冲随昼夜剧变（A 级，我核对过日志）。**但字段含义是推测**（白天 `(-0.4175, 0.7063, 0.5645)` 长度 0.996，更像太阳方向；夜晚同偏移含义变了），只有两个时间点，且同一调用点每帧调用 11 次，"第几次"跨帧是否稳定没验证。他们在 win 上的探针每次调用都加互斥锁，**不能原样合入**。

**待逆向清单：`docs/REVERSE_REQUESTS.md`**；逆向方回复一律**先核对字节再采信**（第 8 节）。已被推翻的结论汇总在 `ELDENRING_REVERSE.md` §29~§32（例：`0x14044E090` 不是"落地复位"；`create.md` 的飞行 Mode 3、`FallModule+0x1D` 免死、BulletParam 数值表不可信；`spawn.md` 第二版的字节大多对得上，但 ID 体系仍以我们实测为准）。

**用户的工作方式（再强调）**：经常 Alt+Tab，失焦是正常的；一次只改一件事、用配置开关二分；只问"能走/不能走/死没死"；**不要在没问的情况下往游戏内存写新字段**；不要凭猜测下结论，先 trace 到产生错误值的那一行；改完文件后先贴验证命令和完整输出，不说"改好了"。

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

**win 构建环境（2026-10-10 起）**：VS 2022 Community 重装过，CMake 缓存已重建为 Windows SDK **10.0.19041.0**（原 22621 残缺）；如果 CMake 报 `could not find specified instance of Visual Studio` 或 `MSB8036` 找不到 SDK，先看 VS 是否在后台安装/卸载（`vswhere -all`），再 `del build-win\CMakeCache.txt` + `rmdir /s /q build-win\CMakeFiles` 后 `cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_VERSION=10.0.19041.0`。用 `copy` 部署时别把它放在 `findstr` 管道后面（会被当作 `findstr` 的参数）。

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
| `eldenring_buddy.hpp` | `CSBuddyMan` 只读采样、`Monitor` 日志、召唤出生点/写入计划、`newEntities` |
| `eldenring_creative.hpp` | 生存/创造模式及其各项差异、"摔落清零/整个跳过击杀包装/滞空超限回答否"的纯判定、探针日志格式 |
| `eldenring_flight.hpp` | 创造飞行：双击空格、方向、单帧积分、`followRebase`（跟随游戏 8 m 坐标重基）、`bodyYaw` |
| `eldenring_chrscan.hpp` | 角色结构只读扫描（RTTI 类名）、`collectChrDispFlagAddresses`（召唤物模型的显示标志地址）、隐藏/还原绘制位 |
| `eldenring_mobs.hpp` | `MobRegistry`：每只召唤物的走路动画、受击红闪、死亡倒地；有模型文件时输出每骨骼矩阵 |
| `eldenring_shadow.hpp` | 脚下阴影：`groundBelow`（射线 + 方块顶面取高）、`shadowAt`（随高度衰减）、`shadowMatrix`、`buildShadowDisc`、`falloff` |
| `eldenring_bullet.hpp` | 弩箭：请求体解码/十六进制转储、`buildFireRequest`（用真实模板构造请求）、`templateUsable/templateChanged`、`boltDamageEr`、`isPlayersBoltHit`、`muzzle`、`LogBudget` |
| `eldenring_handlescan.hpp` | 只读搜索一个 32 位值出现在哪些结构里（含一层指针），当时用来找 `+0x08`；留作通用工具 |
| `eldenring_jump.hpp` | MC 起跳的数值与 `McJumpArc`（起跳实验已搁置，代码保留） |
| `eldenring_fp.hpp` | 第一人称矩阵链（`itemPose`/`eatPose`/`bareArmPose`/`itemDisplay`/`walkBob`/`handSway`）、`HandAnimator`、`SwayFilter`、`toHost`（MC 右手系 → 渲染左手系） |
| `eldenring_particles.hpp` | 粒子系统、受击镜头倾斜曲线 |
| `eldenring_hudtex.hpp` / `eldenring_audio_data.hpp` | 物品→图集格、最近邻放大；声音清单/WAV 解析、脚步节拍 |
| `eldenring_model.hpp` / `eldenring_pick.hpp` | 玩家部件槽（隐藏原模型）、准星选目标 |

`adapters/eldenring/src/`（**Windows 专用，只在 win 上编译**）：`loader.cpp`（总入口、全部钩子、按键线程、HUD 数据）、`overlay_d3d12.cpp`（Present 钩子、ImGui、HUD、粒子、view model 调度）、`steve_renderer_d3d12.cpp`（Steve/手臂/物品 D3D12 渲染；第二张皮肤、`drawMobModel` 通用骨骼绘制）、`input_hook.cpp`（DirectInput）、`audio_xaudio2.cpp`、`png_wic.cpp`。

核心库：`src/entity_model.cpp`（Bedrock geometry JSON 解析、骨骼 pivot/默认旋转/父子矩阵 `boneMatrices`、`mirror`）、`src/combat.cpp`（MC 伤害表、暴击、横扫）、`src/consumables.cpp`（进食）、`src/item_model.cpp`（`buildFlatItemMesh`）、`src/rig.cpp`（Steve 12 部件网格）、`src/hud_layout.cpp`（原版 GUI 缩放排布）。

工具：`tools/extract_mc_assets.py`（皮肤、`--export-mob-skin`、`--export-entity-models`、HUD 图集；加精灵后要重新生成头文件和占位图集）、`tools/bake_assets.py`、`tools/extract_mc_sounds.py`（声音）、`tools/reverse/eldenring/`（只读探针脚本）、`tools/reverse/eldenring/templates/mc_er_hit.bin`（**受击模板备份**，见 5.2）。

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
| 弩箭发射 `spawn_bullet` | `0x3A2CB0` | `sigs::kSpawnBullet` | 只读记录请求体、存模板；F3 也调用它（游戏线程，SEH）。`r9` 是状态码指针 |
| D3D12 | Present / ResizeBuffers / ExecuteCommandLists / CreateDepthStencilView | — | 覆盖层、命令队列捕获、深度缓冲识别 |
| DirectInput | `CreateDevice`、设备 `GetDeviceState/GetDeviceData` | — | 吞鼠标键、取左/右键边沿和滚轮 |

**伤害链（要点）**：点击 → `ClickAttack`（选目标、`MeleeController::makeIntent` 算 MC 伤害 → `erDamage` 换成最大血量百分比）→ `DamageQueue.enqueue`（`tag` 位：1=暴击、2=横扫、4=强击、位 8~15=MC 伤害整数）→ 游戏线程 `ClampDetour` 排空 → 视线检查（射线）→ 构造 `HitContext`（模板见 5.2）→ 调 `vfunc[7]` → 引擎走 `ProcessDamageContext`，我们的钩子把 `ctx+0x228` 换成目标伤害 → 结果回到 `DrainOnce`：命中标记/粒子/音效。

---

## 5. 游戏目录里的东西

### 5.1 开关文件（空文件即可，存在就启用）

`mc_er_overlay.txt`（覆盖层）、`mc_er_damage.txt`（伤害功能）、`mc_er_input.txt`（输入拦截）、`mc_er_hit.bin`（**必须**，见 5.2）、`mc_er_steve.txt`（配置，见下）、`mc_er_physlog.txt`（物理向量日志）、`mc_er_hitlog.txt`（`HIT-IN` 受击日志）、`mc_er_hitvfx.txt`（装受击特效钩子）、`mc_er_buddylog.txt`（召唤管理器只读探针，**会占满一个 CPU 核**）、`mc_er_summon.txt`（让 F2 触发一次群狼召唤）、`mc_er_summonhide.txt`（隐藏召唤物原模型、画 MC 僵尸）、`mc_er_chrscan.txt`（F2 后扫描召唤物结构，只读）、`mc_er_creativelog.txt`（创造模式只读探针：事件分发器、击杀包装等）、`mc_er_bulletlog.txt` / `mc_er_bulletfire.txt` / `mc_er_arrowdmg.txt`（弩箭，见第 0 节）、`mc_er_nolos.txt`（**应急**：关闭墙体遮挡）、`mc_er_anyvictim.txt`（不等受害者被更新，有小概率与 worker 线程竞争）。

### 5.2 `mc_er_hit.bin`（576 字节，必备）

一次真实"玩家→敌人"命中的 `HitContext` 样本（逆向方采集）。伤害功能靠它构造上下文；缺了日志会写 `damage: mc_er_hit.bin missing`。**仓库里的备份：`tools/reverse/eldenring/templates/mc_er_hit.bin`**，装机时复制到游戏目录。其中 `+0x1D0/0x1D8/0x1E0` 是失效指针，每次构造都会覆盖，无害。

### 5.3 `mc_er_steve.txt` 配置键（`key=value`，一行一个）

`occlusion`（1=被场景遮挡）、`depth_const`（**0.00456**，深度×视距常数，实测）、`rel_bias`、`abs_bias`、`debug`/`depthview_gain`/`scene_height`（标定用，保持默认）、`yaw_offset_deg`（**180**，玩家物理四元数朝向与模型相反）、`hide_native`（1=隐藏原模型）、`hide_mask1`（`0x100A1`）、`hide_mask2`（`1`）、`hide_slots`（部件槽位掩码，默认全部）、`sound_volume`（0.8）、`first_person`（1=启动即第一人称）、`eye_height`（1.65）、`fp_persist`（1，持久眼睛相机）、`inv_key`（背包键的虚拟键码，默认 73=`I`）、`inv_sens`（背包指针灵敏度，默认 1）、`no_stagger`（0，实验，见钩子表）、`mc_jump`（0，起跳实验）、`mc_jump_key`（VK，默认空格 32）、`mc_jump_speed`（8.95 m/s）、`blocks`（1，放置/破坏）、`block_collision`（1，软碰撞）、`reach`（4.5 m）、`kb_force`（击退实验，**无效果，别用**）、`no_player_hit_vfx`（配合 `mc_er_hitvfx.txt`）。

### 5.4 热键（游戏窗口在前台时）

`F2` 召唤（需 `mc_er_summon.txt`；僵尸刷怪蛋右键走同一条流程）· `F3` 发射一支弩箭（需 `mc_er_bulletfire.txt` 和 `mc_er_bullet.bin`；400 ms 冷却）· `F4` 截图（**别占用**）· `F5` 生存/创造（MC 模式内）· 双击空格 创造飞行 · `F6` MC 模式开关 · `F7` 切换深度候选（标定） · `F8` 打最近的敌人 · `F9` 逐槽隐藏部件（屏幕黄字显示槽号，`F6`/`F10` 会复位）· `F10` 第一人称 · `F11` 打印部件槽快照 · `F12` 记录 240 帧位置轨迹 · `I` 背包（Esc/再按 `I` 关；打开后：左键拿/放/合并/交换，右键拿一半/放一个，Shift+左键在热键栏和主背包间移动，数字键把悬停格与热键栏对换，物品栏面板上方是 17 种物品的创造选取区）· 滚轮/数字 `1~9` 切热键栏 · 右键 吃东西 · 左键 攻击 · 空格（写死）起跳判定。

---

## 6. 本地资产（**绝不入库**，资产政策见 `mojang-assets-policy` 记忆）

全部放在游戏目录 `mods\mc_adapter\`，缺失时加载器降级（平涂 / 矩形 HUD / 无声）并写日志。

| 文件 | 生成命令（win，仓库根） |
|---|---|
| `steve.png`（64×64）、`mc_hud_atlas.png`（**512×512**，40 个精灵，含 8 个新物品图标和容器面板） | `uv run --python C:\Python313\python.exe --with pillow python tools\extract_mc_assets.py --client-jar D:\game\sekiro\build\mc_client_1.21.8.jar --export-hud-atlas --export-steve-skin --out-dir "<游戏目录>\mods\mc_adapter"` |
| `zombie.png`（64×64，僵尸皮肤，老布局的左臂左腿块是空的，脚本会补成镜像） | 同上命令加 `--export-mob-skin zombie`（**从 client jar 提取，绝不入库**） |
| `models\zombie.geo.json`、`creeper.geo.json`、`skeleton.geo.json`（Bedrock 骨骼模型，**我们自己写的，在 `assets/source/models/entities/`，可入库**） | `uv run --python C:\Python313\python.exe --with pillow python tools\extract_mc_assets.py --export-entity-models --out-dir "<游戏目录>\mods\mc_adapter\models"` |
| `sounds\`（90 个 WAV + `sounds_manifest.txt`） | mac 上：`uv run --with soundfile --with numpy python tools/extract_mc_sounds.py --out-dir <临时目录>`（从 Mojang 资源服务器下载，偶发 TLS 断线会自动重试），再 `scp -r` 到游戏目录 `mods\mc_adapter\` |

图集里现在 51 个精灵（含 `item_zombie_spawn_egg`；真实图集用 1.21.8 jar 里 `item/zombie_spawn_egg.png`，旧 jar 才走灰蛋着色合成）。图集精灵表只往末尾追加（图集 2026-10-09 从 256 扩到 512，所有 UV 数值因此变了，新旧图集文件不能混用，换版本后必须重新生成）；改完要重新生成头文件和占位图集（见第 2 节）。

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
- **弩箭（§34~§35）**：`BulletParam` ID `20007000/20007800/…`（外部资料，实测是 56）；请求体 `+0x08` 发射者、`+0x0C` ID、`+0x20` 目标（实际 `+0x00` 发射者、`+0x08` 参数行 ID、`+0x1C` 子弹 ID）；"`+0x08` 是目标/锁定句柄"；`r9` "无语义"（我们也错过一次：它是状态码指针）；"`+0xB0` 必须为 0"（真实请求里非 0，置 0 实测安全）；"`0x140480EF0` 是阵营判定、玩家与 team 47 互不伤害"（无字节证据）；"`CSBulletIns+0x50` 是矩阵、`VF[6]` 是矩阵读取"（无证据）；命中 `u8[DA]==6`（那是玩家被箭射；打敌人是 2）。
- **光照（§34）**：`d3d12_lighting_capture_hook.hpp` 第一版（钩子从未安装，"真实输出"是单测假数据）；白天 `0x30F5F0400` 对夜晚 `0x351150400`（配对错了：后者是打包数据，且是两个进程）；`+0xC00` 各字段的"太阳强度/仰角/天空漫反射"标签（A 级的是数值，含义是推测）。

---

## 9. 待办（按优先级）

> 2026-10-10：创造模式、召唤僵尸已完成；**当前最新的待办看第 0 节末尾**。下面是更早的长期清单（背包存档、弓箭、HUD 还原等仍有效；"背包界面""创造/飞行/起跳"相关条目已过时）。

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
3. 跑基线：`cmake --build build -j8 && ./build/bin/mc_tests`（715 个应全过）和 Python（`uv run --with pillow --with pytest python -m pytest tests -q`，74 个）。
4. 问用户这次想做什么。第 0 节"未解决 / 待办"里有当前最想做的：弩箭外观与 MC 拉弓输入、盾牌/副手/双持/盔甲、光照（屏幕采样环境光）、召唤/死亡粒子、人形体型的单体骨灰（碰撞体积）、骷髅/苦力怕、站方块走不动；**P0 的 30 分钟稳定性长跑仍没跑过**。
5. 每个功能结束时：更新清单（F 节新增条目）、项目记忆（`~/.claude/projects/-Users-c-mc-core/memory/`）、如果有新逆向结论写进 `ELDENRING_REVERSE.md` 并分级。
