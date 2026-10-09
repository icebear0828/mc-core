# 实验任务：libER 能否作为我们的调度和渲染底座（清单 E1、E2、E3、E4）

> **2026-10-08 更新：阶段 1（E1）已有结论——不支持。** libER `main` 的符号表（`symbols/singletons.csv`）与本机 `eldenring.exe 2.7.1.0` 的全局 RVA 全部错位（差约 `-0x4070`），直接构建会解析到错误地址。本任务书的阶段 2~4 **暂缓**，除非先为 `2.7.1.0` 重做 libER 的符号表。现行方案仍是：`0x140438870`（ClampHP）钩子里仅在主线程排空伤害队列 + `Present`/`ExecuteCommandLists` 钩子渲染。libER 仅作为参考（任务组顺序、`GXDrawTask` 的设计）。

给执行实验的 agent 的任务书。目标是用最小代价回答一个问题：**能不能用 libER 的引擎原生任务（`CSEzTask`、`GXDrawTask`）替代 `0x140438870` 钩子和 `Present` 钩子**。

## 0. 先读，不要跳过

1. 仓库：`/Users/c/mc-core`（macOS 开发；游戏在 Windows 机器上）。
2. 必读文档：`docs/ELDENRING_REVERSE.md` §1.10（队列与深度）、§1.12–§1.13（状态与开源核实）、§3.6（伤害注入结论）；`docs/ELDENRING_VERIFY_CHECKLIST.md`（编号 C*、E*）。
3. 已实现的头文件（纯逻辑，可直接 `#include`，不依赖平台）：`adapters/eldenring/include/` 下的 `eldenring_live.hpp`、`eldenring_world.hpp`、`eldenring_damage.hpp`、`eldenring_state.hpp`、`eldenring_camera.hpp`、`eldenring_rtti.hpp`、`eldenring_singletons.hpp`、`eldenring_sigscan.hpp`。
4. libER：<https://github.com/Dasaav-dsv/libER>（Apache-2.0 + LLVM 例外），文档 <https://dasaav-dsv.github.io/libER/>。读 `README.md`、`include/coresystem/task.hpp`、`include/coresystem/taskgroups.inl`、`include/graphics/draw.hpp`、`examples/`。

## 1. 硬性规则

- 只在**离线、无 EAC、干净安装**的 `eldenring.exe 2.7.1.0` 上做。先备份存档。
- 阶段 1~3 **不得写游戏内存、不得调用游戏函数**（只读和记日志）。阶段 4 才允许调用（只调用已确定的那一个函数，见 §6）。
- 任何一步崩溃：立刻停止，保留日志，写报告，不要“再试一次看看”。
- 不要提交 `hit_7.bin`（真实游戏数据）、不要提交任何游戏资产。
- 不要用 `any`、裸 `python` 等违反用户全局规范的写法；C++ 代码风格跟随仓库现有文件；新增逻辑要有单元测试（能在 mac 上测的部分）。
- 每个阶段结束都写一条结果到 `docs/ELDENRING_LIBER_SPIKE_RESULTS.md`（模板见 §9）。

## 2. 阶段 1：能不能构建、能不能被 2.7.1.0 接受（对应 E1）

做什么：
1. 在 Windows 上 clone libER（`--recurse-submodules`），用 MSVC 或 clang-cl 构建静态或动态库。注意 CMake 要 `add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)`、C++20。
2. 查 `symbols/` 目录：是否有对应 `eldenring.exe 2.7.1.0` 的符号定义文件。**没有就停止，回报“libER 不支持 2.7.1.0”，不要自己硬凑。**
3. 看 README 里 “libER replaces the standard allocators” 这一条，记录它具体在何时、如何替换（是否需要在游戏初始化之前加载）。
4. 写一个最小 DLL（`hello_liber.dll`）：只在加载时初始化 libER 并写一行日志 `libER init ok`。通过 ModEngine2 的 `external_dlls` 加载。

通过标准：游戏正常启动到标题、进入世界、正常游玩 10 分钟，日志里有 `libER init ok`，无崩溃、无明显卡顿。

需要记录：libER 版本（commit hash）、符号文件名、加载方式、编译器版本、启动耗时变化。

## 3. 阶段 2：引擎任务的线程与时机（对应 E2，核心实验）

做什么：用 libER 的 `from::CS::CSEzTask` 派生一个只读的任务类，分别注册到下面这些任务组，**每个注册点各一个独立任务**，只记日志不做任何游戏操作：

| 任务组（`CSTaskGroup`） | 为什么选它 |
|---|---|
| `ChrIns_PostPhysics` | 射线调用候选 |
| `WorldChrMan_PostPhysics` | 对照 |
| `DmgMan_Pre` | 伤害排空候选 |
| `DmgMan_Post` | 对照 |
| `GraphicsStep` | 渲染候选（和 `GXDrawTask` 同组） |
| `FrameEnd` | 对照 |

每个任务在 `eztask_execute` 里记录（用无锁环形缓冲，**不要在回调里做文件 IO**，由另一个线程落盘）：
- `GetCurrentThreadId()`、`QueryPerformanceCounter` 开始和结束时间戳；
- 帧计数（用 `FrameEnd` 任务自增的全局计数器）；
- 本次回调是第几次（每个任务组各自的计数）；
- 回调内是否重入（原子计数当前在回调里的线程数）。

同时（为了判断并发）：保留之前探针里对 `0x140438870`（ClampHP）的只读计数钩子，记录它每次调用的线程 ID 和时间戳。若没有这个探针，可以用现有的 `combat_detour_probe_v6.cpp` 的钩子方式，但**只记数，不改行为**。

跑法：站在野外有敌人的地方 2 分钟；传送一次；死亡复活一次；进地牢一次。

产出表格（写进结果文档）：

| 任务组 | 线程 ID 集合 | 是否固定主线程 | 每帧调用次数分布 | 与 ClampHP 的 worker 调用时间重叠比例 | 重入次数 |
|---|---|---|---|---|---|

判定：
- 如果 `DmgMan_Pre` 固定在一个线程（预期为主线程），每帧恰好 1 次，且与 worker 的 ClampHP 调用**时间不重叠**（或重叠比例 <1%）→ 它是伤害排空的合适位置。
- 如果有重叠，说明这个阶段仍有 worker 在跑，必须保留 “只处理当前更新实体” 的策略，或换到别的任务组再测。
- 如果任务不是每帧都调用（例如读盘、过场时不调用）要明确记录。

## 4. 阶段 3：`GXDrawTask` 与队列、深度的核对（对应 E3、E4）

做什么：
1. 派生 `from::GXBS::GXDrawTask`，`set_scene(UI_SCENE)`，`register_task()`。`draw()` 里只记录：
   - `get_command_queue()` 的指针，**是否等于**之前已验证的 DIRECT 队列（用 `ExecuteCommandLists` 只读钩子的结果对照；两者必须是同一个）；
   - `get_render_target_view()`、`get_depth_stencil_view()` 的 `ptr`；
   - `get_viewport()` 的宽高、`get_scissor_rect()`；
   - `get_delta_time()`；
   - 回调线程 ID 与时间戳。
2. 对 `ID3D12Device::CreateDepthStencilView`（vtable 21）做只读钩子，记录 `句柄.ptr → (资源指针, 资源描述 宽高/格式/SampleDesc, DSV 描述格式)`。在 `draw()` 里用 `get_depth_stencil_view().ptr` 去查，确认得到的资源：
   - 格式 `R32G8X24_TYPELESS`，宽 1920 高 1064（之前实测），DSV 格式 `D32_FLOAT_S8X24_UINT`；
   - 记录 `SampleDesc.Count`（是否 MSAA，之前没说明）。
3. 改分辨率一次（窗口/全屏各一次，或改画质），重复记录，看句柄→资源映射是否重建、视口尺寸如何变化。
4. 再加一个 `HDR_SCENE` 的 `GXDrawTask`，同样只记日志，比较两者的 RTV 是否相同、视口是否相同。

通过标准：
- `GXDrawTask` 的队列等于已验证的队列；
- 每帧恰好回调 1 次；
- 能稳定查到主深度资源；
- 无崩溃。

产出：一张表（回调线程、队列相同与否、RTV 与 DSV 是否稳定、视口尺寸、`SampleDesc`、改分辨率后的变化）。

**可选加分项（不是必做）**：在 `UI_SCENE` 的 `draw()` 里画一个固定位置的半透明矩形（用最小 D3D12 管线或 ImGui DX12 后端），确认它显示在原生 HUD 之上；测帧时间影响（开启前后各 60 秒的平均帧时间，退化要 <5%）。

## 5. 阶段 4：伤害排空（对应 C1、C2、C3，仅当阶段 2 判定 `DmgMan_Pre` 合适）

做什么：
1. 实现 `IMemoryReader`（进程内读取，`VirtualQuery` 检查可读后 `memcpy`，用 SEH 保护，失败返回 `false`）。
2. 单例解析：用 `eldenring_singletons.hpp` 的 `locateGlobalRva` 扫描 `.text`/镜像，解析 `WorldChrMan` 等；用 `readSingleton` 做 RTTI 全名校验。任何一步失败都**不绑定，不继续**。
3. 在 `DmgMan_Pre` 的 `CSEzTask` 里调用 `DamageQueue::drain`：
   - `on_game_thread` 由阶段 2 的结论决定（只有确认是固定的游戏线程才置 `true`）；
   - `require_victim_updating` 初期设为 `false`（因为不再依赖 ClampHP 钩子），但保留 `max_per_drain`；
   - `Invoker`：读受害者伤害模块的 `vtable[7]`（`*(void***)module)[7]`），调用 `void(void* module, void* attacker, void* ctx)`，**只调一次**，不要再调 `ProcessDamageContext`、`vfunc[12]`、`0x140446080`。
4. 命中模板：从本地文件加载 `hit_7.bin`（路径用环境变量，不入库），用 `HitTemplate::fromBytes` 校验长度恰好 `0x240`。
5. 入队来源：F8 键（`GetAsyncKeyState`）触发，在渲染线程 `enqueue(最近敌人, base_damage=50, tick)`；`enumerateEnemies` 挑最近的 `hostile` 敌人（25 米内）。
6. 日志每次记录：入队、`drain` 结果（`DamageOutcome`）、调用前后受害者 HP、`ctx+0x228` 回写值、线程 ID。

必须完成的验证（逐项对应清单）：
- C3：3 个不同敌人，各至少 3 次，扣血、后仰、音效、仇恨转向正常；
- C4：HP 变化量等于 `ctx+0x228`（无重复扣血）；
- C7：对 HP 为 0 的实体入队 → `VictimDead`；
- C8：对中立（team 30）、友好（team 26）入队 → `NotHostile`；
- C2：对 20 个不同敌人排队的等待时间统计（若启用 `require_victim_updating`）；
- C9：连续 30 分钟，含读盘、传送、死亡复活，注入 ≥100 次，无崩溃。

## 6. 停止条件和失败时的回退

- 阶段 1 失败（不支持 2.7.1.0 / 分配器替换造成问题）→ 记录并停止，结论 “libER 不可用，沿用 `ClampHP` 钩子 + `Present` 钩子”。
- 阶段 2 显示所有候选任务组都与 worker 并发 → 结论 “任务阶段不能保证无竞争，必须保留 `require_victim_updating`”。
- 阶段 3 的队列与已验证队列不一致 → 以已验证队列（`ExecuteCommandLists` 观测）为准，`GXDrawTask` 暂不用于渲染。
- 阶段 4 任何崩溃 → 停止，回退到探针方式，不要继续堆次数。

## 7. 交付物

1. `hello_liber` 和实验 DLL 的源码（放 `tools/windows/liber_spike/`，附 CMake；不要放进核心库）。
2. `docs/ELDENRING_LIBER_SPIKE_RESULTS.md`（模板见下）。
3. 原始日志文件的路径（上传到团队共用位置，在结果文档里写链接）。
4. 对 `docs/ELDENRING_VERIFY_CHECKLIST.md` 的建议勾选清单（只写建议，不要自己改勾选；用户和 Claude 核对后才勾）。

## 8. 不要做的事

- 不要在 `eztask_execute` 里做阻塞或文件 IO。
- 不要对游戏内存做任何写入（阶段 4 只通过 `vfunc[7]` 这一个游戏函数间接修改）。
- 不要把 libER 的类型引入 `adapters/eldenring/include/` 的头文件（核心逻辑必须保持不依赖 libER，才能在 mac 上测试）。
- 不要“顺手”优化或重构仓库里已有代码。

## 9. 结果文档模板

```
# ELDENRING_LIBER_SPIKE_RESULTS

## 环境
- eldenring.exe 版本 / SHA256:
- libER commit:
- 编译器:
- 加载方式:

## 阶段 1（E1）
结果: 通过 / 不通过
符号文件:
分配器替换的观察:
启动耗时:
备注:

## 阶段 2（E2）
（表格，见 §3）
结论: DmgMan_Pre 是否合适 / 推荐的任务组 / 线程 ID 是否固定
原始日志:

## 阶段 3（E3、E4）
（表格，见 §4）
结论:
原始日志:

## 阶段 4（C2、C3、C4、C7、C8、C9）
（每项通过或不通过 + 数据）
结论:

## 建议勾选
- E1:
- E2:
- E3:
- E4:
- C1:
- C2:
- C3:
...

## 遇到的问题和意外
```
