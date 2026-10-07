# 新游戏接入手册 (Porting Playbook)

> 本文是把 mc-core 接入一个**新的商业游戏**的完整流程，来自《只狼：影逝二度》的真机接入经验。
> 它回答三个问题：**每一层该做什么、先做什么后做什么、哪里最容易踩坑**。
> 配套文档：架构见 [ARCHITECTURE.md](ARCHITECTURE.md)，工程红线见 [ADAPTER_SPECIFICATION.md](ADAPTER_SPECIFICATION.md)，
> 引擎侦察表见 `skills/mc-game-adapter/references/engine-matrix.md`，逆向工具见 `tools/reverse/`。

---

## 0. 一页纸总览

```
                         ┌──────────────────────── 平台无关，macOS/Linux 可测 ────────────────────────┐
 游戏进程 ──hook/读写──▶  │  loader(Windows)  ─InputSnapshot─▶  mc::Session ──端口──▶  <game>_adapter     │
 (内存、D3D11 Present)   │   • 热键/鼠标                       • 规则引擎全部归它      • 坐标/单位/ID 转换   │
                         │   • 绑定游戏结构(live)              • 一个 tick 驱动一切    • 把意图写成宿主语义   │
                         │   • 3D 渲染 / HUD / 截图                                                      │
                         └──────────────────────────────────────────────────────────────────────────────┘
```

接入一个新游戏的阶段（每阶段都有可检验的产出）：

| 阶段 | 做什么 | 在哪做 | 产出 / 验收 |
|---|---|---|---|
| 0 侦察 | 引擎、加载方式、是否加壳、是否有 RTTI、图形 API、**是否带反作弊** | 纸面 + 真机只读 | `engine-matrix.md` 新条目；确认可离线 |
| 1 离线适配器 | 按模板写适配器与镜像结构，`plugin_entry` 只拼 `Session` | **Mac/Linux**，TDD | 单测全绿，适配器无任何真机依赖 |
| 2 真机逆向 | 找管理器、玩家对象、位置、相机、类名、可见性字段 | Windows 真机，**只读为主** | 一张"已验证事实表"（偏移+证据） |
| 3 绑定层 `*_live` | 把事实变成**带校验**的读取代码 | Mac 上用假内存测 | 校验失败时返回状态，**从不猜、从不回退** |
| 4 坐标与单位 | 宿主 ↔ 规范空间的映射，位置/方向区分，四元数 | Mac 单测 + 真机验证手性 | 测试里写明每个轴的对应 |
| 5 隐藏原模型 | 找到绘制开关，写成 `ModelHider` | 真机找 + Mac 测 | 狼消失/还原，状态可逆，崩溃零次 |
| 6 渲染 | 在游戏画面里画 12 部位 Steve | Windows | 截图里比例/朝向/贴图对 |
| 7 HUD | 原版布局、原版精灵、点采样 | Mac 测布局，Windows 画 | 截图与原版一致 |
| 8 资产 | 从**本地** jar 提取真贴图 | Windows | 不进仓库，日志写明用了哪份 |
| 9 验收 | 逐项勾选 §9 清单 | 真机 | 全绿 |

**铁律：每一阶段结束都要能回答"这个结论的证据是什么"。** 没有证据的偏移、没有测试的转换、没有截图的渲染，都不算完成。

---

## 1. 架构与职责边界

| 层 | 位置（以只狼为例） | 只负责 | 绝对不能 |
|---|---|---|---|
| 规则核心 | `include/mc/`、`src/` | 方块/战斗/弹道/动画/鞘翅/进食/HUD 布局的**规则** | 知道任何游戏；含 Windows API |
| `mc::Session` | `src/session.cpp` | 把所有引擎串成**一个 `tick(dt, InputSnapshot)`**：切槽、放置/挖掘、近战、进食、拉弓、滑翔、动画、身体朝向 | 被适配器绕过、重写 |
| 适配器 | `adapters/<game>/src/<game>_adapter.cpp` | 实现 4 个端口：**把意图翻译成宿主语义**（坐标、单位、ID、受击） | 做玩法决策；直接 cast 游戏内存 |
| 镜像结构 | `<game>_native.hpp` | 适配器操作的**普通 C++ 结构**（`ChrIns` 等），字段是"我们的"数据 | 假装是游戏真实内存布局 |
| 绑定层 | `<game>_live.*` | 通过 `IMemoryReader` 读真实内存，**校验**后填镜像 | 猜偏移；校验失败时用兜底对象 |
| 模型隐藏 | `<game>_model.*` | 经 `IMemoryWriter` 写**一个**已验证字段 | 写未验证的字段 |
| 3D/渲染数学 | `<game>_steve.*` | 矩阵、投影、网格、骨架矩阵，**平台无关可测** | 依赖 D3D |
| 渲染器/loader | `steve_renderer.*`、`dinput8_loader.cpp` | Present hook、D3D11 绘制、热键、截图、HUD 绘制 | 含规则或坐标换算 |
| 插件入口 | `plugin_entry.cpp` | `Session` + 适配器 + 少量 `extern "C"` 导出 | 手写 tick 编排（会漂移，旧设计的教训） |

**为什么要有镜像结构：** 旧版 loader 把游戏内存 `reinterpret_cast` 成项目自己的 C++ 结构（含 `std::string`），再往里写字段，等于按一个假布局往真实内存写，会损坏游戏。正确做法是：适配器永远工作在**镜像**上；loader 每帧把**校验过的**真实读数填进镜像，需要写回游戏时只写**单个已验证字段**。

**契约变更规则：** 为适配某个游戏而去改 `include/mc/contracts/` 是允许的，但必须是**通用的**（对所有游戏都成立）、**有测试**、并**同步所有适配器和 skill 模板**。只狼接入期间加的 `setSteveRoot`、`setLinearVelocity`、`EntityId`，都是这样来的。

---

## 2. 约定：规范空间、单位、ID

### 2.1 规范 MC 空间（核心唯一认可的坐标系）

- **Z 向上、右手系、X 向前、Y 向左、单位厘米。**
- `yaw`：0 = +X，逆时针（向左）为正。`pitch`（给动画器）：**Minecraft 约定，正 = 低头**；`Session` 内部取了 `-asin(fwd.z)`。
- 速度 cm/s；击退力 `knockback_force` 也是 cm/s（与弹道同单位）。
- 适配器必须在**端口边界**把宿主数据换成规范空间，核心里不能出现宿主轴向。

### 2.2 把宿主映射到规范空间：四步

1. **确定手性**（不要猜）：见 §4.5 的实验。左手系→右手系是**反射**。
2. **确定单位**：见 §4.4。只狼是**米**，UE 是厘米。
3. **区分"点"和"方向"**：点/速度/包围盒要换单位（`toNativePoint`），单位方向（法线、相机前向）只换轴（`toNativeDir`）。**两个函数分开命名**，才能让编译器抓出漏改。
4. **旋转要按反射处理**：反射下旋转轴是伪矢量，四元数向量部要取反：`q' = (-M·q.xyz, q.w)`（`M` 为轴变换）。用"同一个点经两种表示旋转后落点一致"的测试证明，而不是凭感觉。

| | 宿主 | 规范空间 | 点（含单位） | 四元数 |
|---|---|---|---|---|
| 只狼 | Y 上、X 右、Z 前，**左手**，米 | Z 上、X 前、Y 左，右手，cm | `native = (-y, z, x) / 100` | `{q.y, -q.z, -q.x, q.w}` |
| UE5（黑神话） | Z 上、X 前、Y 右，**左手**，cm | 同上 | `native = (x, -y, z)` | 向量部 `(-x, y, -z)`，转 `FRotator` 时yaw 取反 |

### 2.3 `EntityId`（强类型，不是 `uint64_t`）

- `None = 0` 表示"没有"，`LocalPlayer = 1` 永远是本地玩家，其它实体由适配器分配 `>= 2`。
- `RaycastResult` 拆成 `hit_entity`（只放已注册实体）和 `hit_collider_handle`（只放我们放的方块）。
- **宿主指针、Havok 句柄、碰撞体句柄绝不能直接当 `EntityId`。** 未注册的命中返回 `None`，不泄漏原始句柄。
- 传给宿主的"忽略实体"要由适配器映射成宿主自己的句柄/Actor，不要把内部 ID 原样传出去。

### 2.4 端口语义（容易写错的几条）

- `processHit` **只改状态**（血量、死亡、躯干值）；击退/硬直由 `CombatEngine::executeHit` 在 `processHit` 成功后**统一触发一次**。适配器里自己再触发一次就会双倍击退。
- `applyLinearImpulse` 是**叠加**，`setLinearVelocity` 是**替换**，别混用。
- `setSteveRoot(脚位置, 身体 yaw)` 先于 `updateStevePartTransforms`，部位变换相对根节点；头部 yaw 是**相对身体**的（Minecraft：头转超过 50° 身体才跟着转）。
- 落地判断拿不到真实标志时，适配器用估计值并在 `InputSnapshot` 里标 `on_ground_is_estimate`（见 §10 踩坑）。

---

## 3. 阶段 0：侦察

在写一行代码之前先回答：

1. **引擎与加载方式**：RAGE/ScriptHookV、REDengine/CET、RE Engine/REFramework、UE/UE4SS、Dantelion/dinput8 代理……查 `engine-matrix.md`。
2. **有没有反作弊**：EAC/BattlEye 等**一律不碰**。必须是可离线的单机游戏，Steam 保持离线。多人/排行榜游戏不做。
3. **可执行文件是否加壳**：看 PE 的 `.text` 熵。**熵 ≈ 8.0 = 磁盘上是加密的**，静态扫描无意义，特征码只能在运行中的进程里找（只狼是这种，带 `.bind` 节）。
4. **有没有保留 MSVC RTTI**：运行时能读出类名（`PlayerIns`、`ChrModel`…）会让后面的工作量降一个数量级。
5. **图形 API**：DX11 好办（Present + ResizeBuffers 两个 hook）；DX12 需要另做。
6. **版本**：记下 exe 版本和 PE 时间戳（只狼：`1.6.0.0` / `0x5fa3e066`）。所有 RVA 都**只对这个版本成立**，换版本要重新验证——所以代码里的校验是必需品，不是装饰。

产出：把结论补进 `engine-matrix.md`。

---

## 4. 阶段 2：真机逆向（方法论）

### 4.1 环境与工作方式

- 开发在 Mac，真机是 Windows，**代码走 git 同步**（push → 真机 pull），不用 scp 传源码。
- Windows 上构建：`VsDevCmd.bat` + `cmake --build build-win --config Release --target <game>_adapter`；输出写日志文件再读（cmd 管道和引号很脆）。
- 部署：把 `dinput8.dll` 复制进游戏目录。**游戏运行时该 DLL 被占用，必须先关游戏。** 日志在游戏目录 `mc_adapter.log`。
- 探测脚本通过 `ssh win 'python -'` 从 stdin 喂入，不在真机留文件；游戏必须在**交互桌面**里运行（ssh 会话启动不了）。
- 需要用户在游戏里做动作时：**先说"回复开始后我监听 60 秒"**。用户要等工具调用结束才能看到消息，所以不能在一个阻塞调用里同时指望他操作。
- 逆向工具在 `tools/reverse/`，每个脚本头部的文档字符串就是它的用法说明（`--help` 同样可用）。

### 4.2 找签名（特征码）

```
python tools/reverse/scan_signature.py --process game.exe --aob "48 8B 35 ?? ?? ?? ?? 44 0F 28 18"
```

- **在运行中的解密映像里扫**。输出里第一行"first code bytes"如果像随机噪声，说明壳还没解密完，等一会再扫。
- 好签名**唯一命中**。命中多处不要挑一个，改成"收集候选 + 结构校验"（只狼的相机就是这样选的）。
- **RIP 相对读**：`mov reg,[rip+rel32]` → 全局变量 RVA = `匹配偏移 + 指令长度 + rel32`。全局里存的是对象指针，**标题界面为 0**，进世界后才有值，所以"值为 0"是正常状态不是失败。
- **别信凭记忆或猜出来的签名。** 只狼旧代码里的 `WorldChrMan` 签名在运行时确实能命中，但命中的是一个无关的分配器对象，`+0x88` 是 0、`+0x80` 是垃圾值；旧 loader 要是在解密后扫到它，会把垃圾当 `ChrIns*` 去写，直接崩游戏。它之前没崩只是因为扫得太早什么都没命中。

### 4.3 找玩家位置（差分快照）

```
ptrtree.py snap --process game.exe --root-global <rva> --root-offsets 0x88 --out a.pkl   # 站着不动
# 在游戏里向前走一段，站定
ptrtree.py snap ... --out b.pkl --like a.pkl
ptrtree.py diff a.pkl b.pkl --mode position
```

- 看**同一组数值在多个路径下重复出现**（真位置通常有好几份拷贝），而且移动量合理。
- 再做**时间序列**（20Hz 采样一段跑动）：位置应该平滑，**加速段的速度要符合游戏实际**（只狼冲刺约 5.6 m/s）。
- 用**两份拷贝互相校验**（`+0x1050` 与 `+0x1060` 差小于 1 米）作为运行时一致性检查。
- 陪衬值也要看：`[+0x20]+0x43c` 那组数值也变了，但水平方向只动了 2.3 米，不是位置。差分法不是"有变化就是它"。

### 4.4 确定单位（必须实测）

- 不要假设厘米。用**已知速度**换算：冲刺位移 / 时间 ≈ 游戏里角色冲刺速度。只狼得到 5.6 单位/秒 → **米**；若是厘米会是 500+。
- 这直接决定碰撞盒（`HalfExtents`）、弹道、速度阈值的换算，漏掉会让一个 1 米方块变成 100 米。

### 4.5 找相机并确定手性

- **相机矩阵**：让镜头水平转动，找**长度为 1 且方向变了**的 float 三元组：`ptrtree.py diff --mode unit-vector`。会出现一对互相垂直的向量，旁边就是 4×4 行主序矩阵：右、上、前、位置（`w=1`）。
- **结构校验**（运行时也要做）：三行长度 1、两两垂直，第四行 `w=1` 且位置有限。
- **验证它真是游戏相机**：相机到玩家的距离应是合理的第三人称距离（只狼约 4.4 米），"相机→玩家"方向与前向量夹角余弦接近 1。
- **FOV**：相机对象里常有 FOV 的 getter（只狼：`movss xmm0,[rax+0x160]; ret`）。读出后做合理性校验（0.2~2.6 rad），**读不到就用兜底值，不要让一个怪 FOV 让整次采样作废**。用"Steve 在屏幕上的像素高度是否等于 1.8 米按 FOV 算出的高度"验算。
- **手性实验（不能靠猜）**：让用户**把镜头向右转**，看前向量的变化量与"第 0 行"的点积——为正说明第 0 行就是相机右侧。第 0 行是 +X 且前向是 +Z，就是**左手系**（右手系里前向 +Z 时右侧是 −X）。

### 4.6 用 RTTI 给对象起名

```
python tools/reverse/rtti_survey.py --process game.exe --root-global <rva> --root-offsets 0x88 --depth 3
```

- 多态对象的第一个 8 字节是虚表指针，虚表前 8 字节指向 RTTI 的 "complete object locator"，里面能读到类名。
- 一次调查就把无名的堆指针变成地图：`ChrModel`、`SprjModelDrawEntity`、`SprjAsmModelDrawEntity`、`PlayerCtrl`、`FdpChrFaceAnimModule`……候选对象从 25 万个浮点缩到几个类。
- 同时能看继承链（`SprjAsmModelDrawEntity ← SprjDrawEntity ← DrawEntity ← GXSgEntity ← GXSgDrawable…`），知道哪些成员在**共同基类**里（两个子类里偏移相同、函数地址相同）。

### 4.7 用虚表里的短函数读出字段偏移

```
uv run --with capstone python tools/reverse/vtable_dump.py --process game.exe --root-global <rva> --root-offsets 0x88,0x48,0x250
```

- 反汇编虚表前 N 项，重点看**几条指令就 `ret` 的函数**：`mov dword ptr [rcx+0x70], edx ; ret` 就是一个 `int` setter，`mov byte ptr [rcx+0x826], dl ; ret` 是 bool setter。**字段的偏移和类型直接就写在汇编里。**
- 注意 `this` 可能是子对象：`add rcx,0x240 ; jmp …` 这类 thunk 会调整 `this`。**读出的当前值必须长得像 setter 写的类型**（读到 `0xbee4f90e` 这种像浮点的值说明偏移对应的是别的子对象）。

### 4.8 找"隐藏模型"的开关：什么能写，什么不能写

**走过的错路（别重蹈）：**

1. 把玩家对象指针树里所有恰好等于 `1.0f` 的浮点（透明度/缩放常是 1.0）**批量写成 0**。候选有 **257,380 个**，按批二分要几千次；而且**游戏第 15 次试写就崩了**（`sekiro.exe` 内访问违例）。
2. 把范围缩到 RTTI 点名的几个对象、304 个候选（浮点 1.0 和值为 1 的字节），**第二次试写又崩**，崩溃点不同（`+0x65c4a0` 与 `+0x9f1467`）。结论：**往未知字段写 0 会写坏游戏内部状态**，不是某一个特别危险的字段。

**对的做法：**

1. 用 RTTI 找到绘制相关对象（`SprjAsmModelDrawEntity`）。
2. 反汇编它的虚表，列出 setter；**先只读当前值**，看是否符合 setter 的类型。
3. **只写游戏自己的 setter 会写的字段，写 setter 会写的值**，一次一个字段，写完立刻还原。
4. 用截图**肉眼确认**，再下结论。

结果：`[[[WorldChrMan+0x88]+0x48]+0x250]+0x70` 是 `int` 绘制掩码（可见时为 4），写 0 整个狼（身体、围巾、刀、刀鞘、影子）消失，还原即恢复，0.4 秒后仍有效（游戏没有每帧覆盖）。虚表里 `[19] mov dword ptr [rcx+0x70], edx ; ret` 一开始就指着它，只是当时不知道它的含义。

**判断标准（写内存之前问自己）：**

- 我是否知道这个字段的**类型和含义**？（来自 setter / 当前值，而不是"碰巧等于 1"）
- 我写的值是否是游戏**自己会写的值**？
- 我是否**记得原值**并能还原？
- 这个对象的 **RTTI 类名**是否是预期的？

### 4.9 用截图验证实验（以及它的坑）

- overlay 里按 **F7** 或在游戏目录创建 `mc_cmd_screenshot.txt` 可保存当前画面（ssh 没法替你按键，所以做了文件触发）。`tools/reverse/screen_probe.py` 封装了拍照、投影出角色所在矩形、计算差分。
- **先测基线噪点**（同一场景连拍几张），阈值用 `max(6, 4×噪点)`。
- **暗角色在暗背景下，像素差分分数极低。** 狼完全消失时分数只有 2.8（噪点 0.5，我设的阈值 6），差分图几乎全黑。**一定要看保存下来的图**，不要只信分数。
- DXGI 格式 24 = `R10G10B10A2_UNORM`，要手动解包；PNG 编码器只收 BGRA，`SetPixelFormat` 会**悄悄**改格式而不转换数据，必须检查返回的 GUID，否则红蓝互换（红心变蓝心）。

---

## 5. 阶段 3：绑定层的写法（`<game>_live`）

把 §4 的事实写成代码时，**所有读取都经 `IMemoryReader`**，这样可以用假内存在 Mac 上测完所有分支。规则：

1. **扫描直到解密完成**：每次 `scan()` 返回 `Searching`/`Ambiguous`/`Bound`；壳没解密时特征码自然找不到，继续等，不超时放弃也不猜。
2. **唯一命中才绑定**；多处命中返回 `Ambiguous`，**拒绝猜**。候选多的（相机）收集后用**结构校验**选。
3. **采样返回明确状态**（`NotBound / NotInWorld / Invalid / Ok`），任何一项不合格都**不使用数据**。校验包括：有限且有界、两份位置一致、相机三行正交、`w=1`、FOV 合理。
4. **没有兜底对象。** 旧设计在特征码失败时退回一个"影子玩家"，结果界面显示 Active、实际改的是自己的内存，故障被完全掩盖。现在链接未建立就**不 tick**，并在面板里写明原因。
5. **进世界瞬间的假数据**：对象已存在但位置还没写入时两份拷贝都是 `(0,0,0)`，视为"还没进世界"。
6. **速度用位置差分**得到（游戏里没有可读的速度字段），并对传送（速度 > 200 m/s）清零，`dt≈0` 防除零。
7. 读取用 `ReadProcessMemory(GetCurrentProcess())`：无法读就返回失败而不是崩溃。

写入同理：`IMemoryWriter` 用 `WriteProcessMemory`，写失败返回状态；**`ModelHider` 的完整规则**：

- 写之前校验 RTTI 类名和当前值（小的非零整数）；
- **记住原值**，还原时写回；
- 游戏在隐藏期间把值改回去 → 重新写 0；
- 还原后游戏自己改成别的值 → **不要覆盖**；
- 对象换了（读档/换场景）→ 丢弃记住的原值，不去写已经不存在的对象；
- 世界退出用 `forgetObject()`，DLL 卸载用 `restore()`。

---

## 6. 阶段 6：把 Steve 画进游戏世界

### 6.1 模型（平台无关，可测）

- 12 部位盒子表与 `tools/extract_mc_assets.py` 的 `PARTS` **同一张表**（枢轴、尺寸、皮肤 UV 起点、膨胀量）。`origin` 相对 `pivot`（Minecraft 的 `ModelPart` 约定）。
- 模型像素 → 宿主：Minecraft 模型空间（x=角色左、y=向下、z=向后）；32 像素 = 1.8 米；脚在地面 `y=24`。到只狼空间是 `(-mx, 24-my, -mz) × 0.05625`。**这个映射是反射**，所以三角形绕序要按几何法线（指向盒子中心之外）自己算，渲染时关闭背面剔除。
- **用仓库里已有的 OBJ 交叉校验**：12 个 OBJ 的每个顶点的 UV 与 C++ 面表逐点比对，一致才说明皮肤排布和 Python 工具相同。覆盖层（帽子、外套、袖子、裤子）在占位皮肤里是透明的所以 OBJ 为空，改用**标准 64×64 皮肤布局的硬编码值**独立校验。

### 6.2 矩阵约定

- **行向量**（`v' = v * M`，与 D3D 一致），行主序上传，HLSL 里 `row_major` + `mul(v, M)`。
- 相机视图矩阵直接用游戏的相机世界矩阵（右、上、前、位置）求逆；投影用**左手系透视**，`ys = 1/tan(fov/2)`，`xs = ys/aspect`（FOV 是**竖直**的，由屏幕高度验算得到）。
- 骨架部件矩阵：`平移(-枢轴) × 旋转(动画四元数) × 平移(枢轴) × 绕竖直轴转身体 yaw × 平移(根位置)`。

### 6.3 渲染器要点

- 着色器用 `D3DCompile` 在运行时编译，一个顶点缓冲装 12 个部位，每个部位一个世界矩阵常量缓冲。
- **用自己的深度缓冲**（每次 resize 重建）保证 Steve 自身遮挡正确；**被场景遮挡**见 §6.5。
- **完整保存/恢复管线状态**（RS/Blend/DepthStencil/RTV/DSV/IA/各阶段着色器与常量/SRV/采样器，含 GS/HS/DS 置空），否则会干扰游戏下一帧。
- 覆盖层用 `clip(alpha-0.5)` 做镂空，不做混合。点采样保持像素风。

### 6.5 被场景遮挡：找到并使用游戏的深度缓冲

只狼实测的方法与坑，换游戏照做：

1. **钩上下文，不是钩设备**：对 `ID3D11DeviceContext` 的 vtable 33（`OMSetRenderTargets`）、34（`…AndUnorderedAccessViews`）、53（`ClearDepthStencilView`）下 MinHook，记录被绑定过的 DSV（尺寸、格式、绑定次数）。只钩立即上下文会只看到一个"只含角色"的小缓冲；**游戏在延迟上下文里画场景**，要钩 `ID3D11Device::CreateDeferredContext`（vtable 27），在第一个延迟上下文创建时再钩它自己的 33/34/53（每组钩子需要各自的 trampoline）。
2. **转储与辨认**：按文件触发时把候选深度贴图 `CopyResource` 到 staging 读回，打印范围和 16x9 网格；候选里与屏幕等大、绑定次数最多、带 SRV 位的就是主场景深度。看清清除值：**清成 0.0 就是反向 Z**（越近值越大）。
3. **标定投影**：反向 Z 且远平面近似无穷时 `深度 × 视空间 z` 是常数。用已知位置的物体（玩家身上沿竖直轴取一串点）投影到屏幕、读该像素深度，得到常数（只狼约 0.0806，狼身厚度带来约 ±4% 误差）。
4. **在着色器里比较**：给深度贴图建 SRV（`R32G8X24_TYPELESS` → `R32_FLOAT_X8X24_TYPELESS`），像素着色器 `Load(SV_Position.xy)`，视空间 z = `rcp(SV_Position.w)`（像素着色器里的 w 是 1/w_clip）。被遮挡判定 `n/深度 < z·(1-相对偏置) - 绝对偏置`，用**相对偏置**吸收标定误差（脚贴地面不会被切掉）。纯函数在 `sceneOccludes`，有单元测试。
5. **光照匹配**：把当前帧 `CopySubresourceRegion` 到带 mip 链的纹理，着色器在 Steve 胸口附近取低 mip 的平均色，按亮度调曝光、按色偏着色，再加 MC 的分面明暗和略降饱和度，否则自绘角色在暗淡的游戏里会"格格不入"。

### 6.4 朝向与动画的接线

- 身体朝向由 `Session` 持有：头可相对身体转到 50°，超过身体跟着转，**跨 ±π 走短路径**；适配器通过 `setSteveRoot` 拿到。
- 目前身体朝向 = 相机水平朝向，不是角色真实朝向（真实朝向字段还没找，见 §11）。

---

## 7. 阶段 7：HUD 要做得像原版

HUD 之所以看起来"像贴在游戏上"，是因为以下几点每一点都不对：

1. **采样**：ImGui 的 DX11 后端对所有贴图用**双线性**采样，9×9 的红心放大后是糊的。用 `AddCallback` 在 HUD 绘制期间换成**点采样**，结束后 `ImDrawCallback_ResetRenderState`。
2. **整数缩放**：`HudLayout::guiScaleFor(h) = max(1, round(h/360))`（1080p → 3）。所有长度都是 GUI 像素的整数倍，坐标是整数。
3. **原版排布**（数值在 `HudLayout` 的测试里）：快捷栏 182×22 居中贴底；选中框 24×23，比槽位外扩 1 像素；物品 16×16，槽位内偏移 3；红心间距 **8** 像素、图标 **9** 像素（图标**重叠 1 像素**）；鸡腿**从右往左**，最右一个与快捷栏右边缘齐平；状态行在屏幕底上方 39 个 GUI 像素。
4. **精灵集合**：红心/鸡腿各有**容器、满、半**三态，容器是深色轮廓底图；整条快捷栏用一张 182×22 的原图，不要切成 9 个小方块；方块物品用**等距小方块图标**（顶面、左面 0.78、右面 0.6 的明暗）。
5. **物品名只在切换后显示 2 秒再淡出**，带原版式投影（同一文字向右下偏 1 个 GUI 像素的深灰）。

调试面板默认隐藏（F8 切换），只有在链接失败时自动显示，让用户知道为什么。

---

## 8. 阶段 8：资产与法务

- **真实的 Minecraft 贴图（HUD 精灵、Steve 皮肤）永远只在本地生成，不进仓库、不推 GitHub。**
- 仓库里的 `assets/source/textures/*.png` 与内嵌在 `include/mc/hud_atlas.hpp` 的图集都是**程序合成的占位图**，且有测试要求"用工具重新生成的结果逐字节等于已提交的头文件和 PNG"，防止手改漂移。
- 本地提取：

```
uv run --with pillow python tools/extract_mc_assets.py --client-jar <client.jar> --export-hud-atlas --out-dir "<游戏>/mods/mc_adapter"
uv run --with pillow python tools/extract_mc_assets.py --client-jar <client.jar> --export-steve-skin --out-dir "<游戏>/mods/mc_adapter"
```

- loader 优先读 `mods/mc_adapter/mc_hud_atlas.png`、`steve.png`，找不到才用占位，**并在日志里写明用的是哪个**（`external file` / `EMBEDDED PLACEHOLDER`）。
- 图集布局**不依赖有没有 jar**（先按固定顺序打包，再决定像素来源），所以 C++ 里的 UV 常量对两种图集都成立。精灵之间留 1 像素透明间隔，点采样不会串色。

---

## 9. 验收清单（新游戏必须逐项勾选）

继承 `ADAPTER_SPECIFICATION.md` 的 C1–C7，再加：

- [ ] **C8 绑定可靠**：特征码在**解密后**命中且唯一；校验失败时日志写明状态，**没有兜底对象**。
- [ ] **C9 单位/手性有证据**：用实测（速度、转镜头）确定，而不是假设；转换有单测。
- [ ] **C10 写内存有证据**：每个被写的字段都有 RTTI/setter/当前值作依据，且可还原；**没有"批量盲写"**。
- [ ] **C11 渲染有截图**：F7 截图里 Steve 与角色位置/比例对得上，颜色正确（红心是红的）。
- [ ] **C12 HUD 点采样**：放大后像素是硬边，布局与原版一致。
- [ ] **C13 资产不入库**：`git status` 里没有真实 Mojang 素材；日志写明贴图来源。
- [ ] **C14 测试**：核心 + 适配器 + live（假内存）+ 数学全绿；`pytest tests/test_adapter_standards.py` 通过。
- [ ] **C15 干净退出**：卸载 DLL 时狼/原角色恢复，不残留状态。
- [ ] 在真机上**连续几次**进出世界、切换模式、读档，没有崩溃。

---

## 10. 踩坑清单（症状 → 原因 → 修复）

| 症状 | 原因 | 修复 |
|---|---|---|
| 日志里没有"绑定成功"，界面却显示 Active | 特征码扫得太早（壳没解密）/ 命中的是误报；旧代码退回影子对象 | 持续扫描直到解密；唯一命中；删掉兜底对象 |
| 旧签名命中但 `+0x88` 是 0、`+0x80` 是垃圾 | 签名凭猜写的，命中了无关对象 | 运行时验证每个签名；用结构校验 |
| 方块变成 100 米 / 速度差 100 倍 | 没做单位换算（只狼是米） | 实测单位；点和方向分开换算 |
| Steve 左右镜像、转身方向反 | 左手系 ↔ 右手系是反射，只换了轴没处理手性/旋转 | 实验确定手性；四元数向量部取反；单测"落点一致" |
| 抬头时 Steve 低头，拉弓手臂往下压 | 动画器用 Minecraft 俯仰约定（正=低头），传进去的是正=抬头 | `Session` 里取反，并写明约定 |
| 头转了两次 | 动画器收到的是绝对 yaw，身体不转 | 身体 yaw 放进 `Session`，头 yaw 相对身体 |
| 击退是预期的 2 倍 | `processHit` 里和 `executeHit` 里各触发一次 | 约定：只有核心触发 |
| 击退放大 100 倍 | 适配器里 `force * 100`，而核心的单位已经是 cm/s | 直接按 cm/s 施加 |
| 玩家无法成为受击目标 | `EntityId 0` 既是"没有"又是玩家 | `None=0`、`LocalPlayer=1` |
| 滑翔下一帧就被取消 | 我们每帧写水平速度，垂直速度估计值读到"在地面" | `on_ground_is_estimate`：估计值不取消滑翔，着陆交给射线 |
| 红心/金苹果变蓝（截图） | PNG 编码器悄悄改成 BGRA 却没转数据 | 写 BGRA，并检查 `SetPixelFormat` 返回的格式 |
| HUD 糊、像贴纸 | 双线性采样；非整数缩放；缺容器/半心；间距不对 | 点采样回调；`HudLayout`；补精灵 |
| "UI 是手绘的" | 仓库里所有"原版"贴图其实是程序合成的占位图 | 从本地 jar 生成真图集，运行时优先加载 |
| 游戏崩溃（`c0000005`） | 往未知字段**批量盲写** | 只写 setter 能写的字段（§4.8） |
| 狼完全消失但差分分数只有 2.8 | 暗角色/暗背景 | 看图，别只看分数；阈值按噪点定 |
| 复制 DLL 失败"另一个程序正在使用" | 游戏还开着，DLL 被占用 | 先关游戏再部署 |
| `cmd` 里 `tail`/管道/引号出错 | 远端是 cmd，不是 bash | 输出写日志文件再读；复杂脚本从 stdin 喂 |
| `uv run` 报 Python 3.14 目录损坏 | 真机上 uv 管理的解释器坏了 | `--python C:\Python313\python.exe` |
| 中文输出乱码 | cmd 输出是 GBK | `| iconv -f GBK -t UTF-8 -c` |
| `GSGetShader` 编译错误 | D3D11 的 Get 系列要 3 个参数 | `GSGetShader(&p, nullptr, nullptr)` |
| 工具调用期间用户"没反应" | 用户要等调用结束才看到消息 | 先让用户回复"开始"，再监听 |

---

## 11. 只狼案例：已验证事实与文件地图

> 版本：Steam `sekiro.exe` 1.6.0.0（PE 时间戳 `0x5fa3e066`）。RVA 只对该版本成立。验证日期 2026-10-06/07。

| 项 | 值 |
|---|---|
| 单位 / 手性 | **米**；Y 上、X 右、Z 前，**左手** |
| WorldChrMan | AOB `48 8B 35 ?? ?? ?? ?? 44 0F 28 18`，唯一命中；全局 RVA `0x3d7a1e0`（标题界面为 0） |
| 玩家 ChrIns | `[WorldChrMan+0x88]`；类名 `PlayerIns@NS_SPRJ` |
| 玩家位置 | `ChrIns+0x1050`（`+0x1060` 是拷贝，差 < 1 m） |
| 相机对象 | AOB `48 8B 05 ?? ?? ?? ?? 48 85 C0 74 09 F3 0F 10 80 60 01 00 00 C3` 找候选全局（本机 RVA `0x3d6ce08`），结构校验选定 |
| 相机矩阵 | 对象 `+0xea0`：右、上、前、位置（行主序，`w=1`） |
| 相机 FOV | 对象 `+0x160`，竖直，实测 0.7 rad |
| 模型对象链 | `ChrIns+0x48` = `ChrModel`；`ChrModel+0x250` = `SprjAsmModelDrawEntity`（整个狼）；`ChrModel+0x10` = `SprjModelDrawEntity` |
| **隐藏狼** | `SprjAsmModelDrawEntity+0x70`（`int`，4=绘制），写 0 隐藏、写回还原 |
| 未知 | 角色真实朝向、敌人列表、着地标志、游戏深度缓冲 |

**文件地图：**

| 文件 | 内容 |
|---|---|
| `adapters/sekiro/include/sekiro_native.hpp` | 镜像结构（`ChrIns`、`ChrCam`…），**不是**游戏真实布局 |
| `adapters/sekiro/include/sekiro_adapter.hpp` / `adapters/sekiro/src/sekiro_adapter.cpp` | 4 个端口的实现；`toNativePoint/Dir`、`toMcPoint/Dir`、`toNativeQuat` |
| `adapters/sekiro/include/sekiro_live.hpp` / `adapters/sekiro/src/sekiro_live.cpp` | `LiveBinder`（扫描+校验）、`LiveMirror`（填镜像、差分速度）、`IMemoryReader` |
| `adapters/sekiro/include/sekiro_model.hpp` / `adapters/sekiro/src/sekiro_model.cpp` | `ModelHider`、`rttiClassName`、`IMemoryWriter` |
| `adapters/sekiro/include/sekiro_steve.hpp` / `adapters/sekiro/src/sekiro_steve.cpp` | 12 部位模型表、矩阵数学、投影、骨架矩阵 |
| `adapters/sekiro/include/steve_renderer.hpp` / `adapters/sekiro/src/steve_renderer.cpp` | D3D11 渲染器（Windows） |
| `adapters/sekiro/src/dinput8_loader.cpp` | Present/ResizeBuffers hook、热键、截图、HUD 绘制、把各部分接起来 |
| `adapters/sekiro/src/plugin_entry.cpp` | `Session` + 适配器 + `extern "C"` 导出 |

**核心侧的相关文件：** `include/mc/session.hpp`、`include/mc/hud_layout.hpp`、`include/mc/types.hpp`（`EntityId`、`RaycastResult`）、`include/mc/contracts/*`。

**热键：** F6 Steve 模式开关，F7 截图（也可由 `mc_cmd_screenshot.txt` 触发），F8 调试面板。

**这次的真实时间线（也是推荐顺序）：**

1. 审计旧代码 → 发现功能"没接通"、影子回退、ID 混用、单位/手性没处理。
2. 写 `Session`（离线、TDD）→ Sekiro/Wukong 适配器接入 → 修契约缺口（击退、`EntityId`、速度端口）。
3. 上真机：静态扫描无意义（加壳）→ 运行时扫描 → 发现旧签名是误报。
4. 差分找位置/相机，实测单位和手性，核对 FOV。
5. 写 `*_live`（带校验）→ 部署 → 日志证明绑定成功。
6. 加载真素材（本地 jar）→ 修 HUD 与截图 bug。
7. 3D Steve：数学（Mac 测）→ 渲染器（Windows）→ 截图验算比例。
8. 隐藏狼：盲写崩两次 → 改 RTTI + 虚表 + 单字段实验 → `ModelHider`。
9. 整理工具与文档。

---

### 11.x 逐帧数据抓住的两个"抖动"坑

- **相机对象只每两帧更新一次**（只狼 30 Hz，玩家位置每帧）：用"过期相机 + 新玩家位置"投影，角色每帧在屏幕上来回跳。症状是"走动时抖"。方法：加一个文件触发的逐帧记录（`mc_cmd_trace.txt` → `mc_trace.csv`），看玩家/相机每帧位移是否交替为 0。修复是 `CameraStabilizer`：相机没变的那帧按上次的相机-玩家偏移外推（最多 2 帧）。
- **别按固定窗口里的"速度"驱动肢体**：宿主位置按步长更新，逐帧求速度会在 0 和 2 倍之间跳，Session 对动画速度做 0.12 s 低通，并沿宿主身体方向而不是相机方向分解。

### 6.6 真实朝向的找法

不要凭一次对拍下结论：同一个字段在不同次采样里符号可能翻转（四元数双覆盖、或读到的只是矩阵的一部分）。可靠做法：边跑边录（`rec` 约 30 Hz，持续 9 秒，期间慢慢转镜头），用 `tools/reverse/facing.py` 的思路，对所有"单位二维向量"候选与**跑动方向**做相关，只取集中度高且偏移恒定的；然后检查它周围的结构（这次发现是 3x4 `[R|t]` 变换矩阵，平移等于玩家位置，用来做校验）。

## 12. 已知缺口（新游戏也会遇到）

- **敌人枚举**：游戏里近战打不到东西，需要从游戏的实体列表读出敌人并 `RegisterEntity`。
- **真实血量/饥饿**：HUD 显示的是预设值，未读游戏数据。
- **着地标志**：目前是"垂直速度小"的估计值（`on_ground_is_estimate`）。
- **Minecraft 位图字体**：标签和计数用 ImGui 字体。
- **滑翔对宿主的影响**：写 `Velocity` 到镜像；真实游戏的移动是动画驱动的，需要找到角色控制器字段。
- **Wukong**：只有适配器与 `Session` 接线，没有 loader，也没有在真实游戏上验证过。
