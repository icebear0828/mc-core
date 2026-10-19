# 控制策略层（Control Policy）

> **先读这一页再改模式、输入或第一人称。**
>
> 以前每加一个模式（第一人称、CS、飞行、背包、滑翔……）都要在
> `hotkey_dispatcher.cpp` 里加一段 `if`，再在 `player_locomotion.cpp`、
> `loader.cpp` 里各加一次判断。现在这些判断统一收口到
> `include/mc/control_policy.hpp` 和
> `adapters/eldenring/include/eldenring_control_policy.hpp`。

## 1. 它解决什么问题

同一时刻可能有好几个功能都想接管玩家的输入：

- 第一人称想接管相机；
- MC 跳跃想屏蔽原生空格；
- 创造飞行想屏蔽 WASD/空格/Shift/Ctrl；
- CS 模式想屏蔽 R/F/1-4 和滚轮；
- 背包打开时想屏蔽鼠标移动和键盘；
- 菜单/加载画面打开时，又必须把输入还给游戏。

如果每个功能自己写 `if`，就会出现“谁先判断、谁覆盖谁”的隐式规则，
新 mod 只能靠复制旧代码。控制策略层把这件事变成一次显式的仲裁：

```text
Feature（MC / CS / FirstPerson / Flight / Inventory / ...）
    │  提交 ControlRequest { domain, authority, priority, key }
    ▼
ControlResolver（纯 C++，按 priority 合并，可单测）
    │
    ▼
ControlPlan（每帧一份：各 domain 的 authority + 要屏蔽的键）
    │
    ├─ hotkey_dispatcher：SetMaskedKeys / SetSuppressMouseButtons / SetSuppressMouseWheel
    ├─ player_locomotion：相机/移动/跳跃的 authority
    └─ loader：第一人称相机写入
```

**决策留在 core，宿主只负责执行。**

## 2. 核心类型

文件：`include/mc/control_policy.hpp`

| 类型 | 作用 |
|---|---|
| `ControlDomain` | 被仲裁的域：`MouseButtons`、`MouseWheel`、`MouseMotion`、`Keyboard`、`Key`、`Camera`、`Movement`、`Jump`、`Attack`、`Use`、`Roll`、`Sprint`、`Sneak`、`Menu`、`Hotbar`、`Cursor` |
| `ControlAuthority` | `Native`（游戏自己管）、`Shared`（两边都动）、`Mod`（mod 独占，游戏不许动）、`None`（冻结） |
| `ControlRequest` | 一次声明：`{domain, authority, priority, key, reason}` |
| `ControlPlan` | 仲裁结果；`blocksNative(domain)` / `owns(domain)` / `masked_keys` |
| `ControlResolver` | 收集 request，`resolve()` 出 plan |

规则：

- **priority 大的赢**；同 priority 时，**后注册的赢**。
- `ControlDomain::Key` 且 `key > 0` 表示屏蔽单个键；`key <= 0` 忽略。
  键的编号由适配器定义（Elden Ring 用 DirectInput scan code）。
- `blocksNative(domain)`：`Mod` 或 `None` 都为 true。
- `owns(domain)`：只有 `Mod` 为 true。
- `packedAuthorities()`：每个 domain 2 bit，适配器可以塞进一个
  `std::atomic<uint64_t>`，跨线程无锁读。

## 3. Elden Ring 当前模式矩阵

这张表是审查用的快照；**代码以 `eldenring_control_policy.hpp` 为准**。
`Mod*` 表示只有 `WantSuppress()` 为真（没有菜单/加载画面）时才生效。

| Domain | 默认 | MC 模式 | CS 模式 | 第一人称 | 创造飞行 | 背包打开 |
|---|---|---|---|---|---|---|
| MouseButtons | Native | Mod* | Mod* | 不变 | 不变 | Native（背包自己处理） |
| MouseWheel | Native | Native | Mod | 不变 | 不变 | Native（CS 仍 Mod） |
| MouseMotion | Native | Native | Native | 不变 | 不变 | Mod（InventoryController） |
| Keyboard | Native | Native | Native | 不变 | 不变 | Mod（InventoryController） |
| Camera | Native | Native | Native | **Mod** | 不变 | Native |
| Movement | Native | **Shared** | Native | **Shared** | **Mod** | Native |
| Jump | Native | **None**（`mc_jump` 开） | Native | 同 MC | **Mod** | Native |
| Roll | Native | **None**（禁用翻滚） | Native | 同 MC | Native | Native |
| Attack / Use | Native | Mod* | Mod* | 不变 | 不变 | Native |
| 单键屏蔽 | 无 | Space（`mc_jump`）、Shift（禁用翻滚） | R/F/1-4 | 无 | WASD/Space/Shift/Ctrl | 无 |

说明：

- **第一人称**：`Camera = Mod`，`Movement = Shared`。游戏仍然负责碰撞，
  mod 负责写期望速度和身体朝向。如果以后要让第一人称完全接管移动，
  在 `requestFirstPerson` 里把 `Movement` 改成 `Mod` 或 `None`，并先在
  真机验证碰撞和台阶。
- **MC 移动**：`Movement = Shared`。mod 写期望速度和身体朝向，宿主仍然负责
  碰撞、台阶和动画；不是 `Mod`，因为宿主没有完全被关掉。
- **MC 跳跃 / 翻滚**：`mc_jump` 开时 `Jump = None` 并屏蔽空格；禁用翻滚时
  `Roll = None` 并屏蔽 Shift。原生前摇和翻滚都不会启动。
- **创造飞行**：`Movement = Mod`、`Jump = Mod`，并屏蔽 WASD/空格/Shift/Ctrl；
  飞行模块自己积分位置。
- **菜单/加载**：`WantSuppress()` 为假时，鼠标/攻击/使用回到 `Native`，
  否则玩家在菜单里点不了。
- **背包**：打开时由 `mc::requestInventory` 请求
  `MouseMotion` / `Keyboard` / `Cursor = Mod`，dispatcher 统一应用
  `SetSuppressMouseMotion` / `SetSuppressKeyboard`；`InventoryController`
  只负责开关和 UI 逻辑，不再直接碰宿主输入。

## 4. 新增一个模式（推荐流程）

以“新增一个 `aim_mode`（瞄准时锁相机、屏蔽跳跃）”为例。

### 第 1 步：在适配器的 policy builder 里声明

文件：`adapters/eldenring/include/eldenring_control_policy.hpp`

```cpp
struct ModeInputs {
    // ...
    bool aim_mode{false};
};

inline mc::ControlPlan buildControlPlan(const ModeInputs& in) {
    mc::ControlResolver resolver;
    // ... 现有请求 ...

    if (in.aim_mode) {
        resolver.request(mc::ControlDomain::Camera, mc::ControlAuthority::Mod, 120, 0, "aim mode");
        resolver.request(mc::ControlDomain::Jump, mc::ControlAuthority::None, 120, 0, "aim mode");
    }
    return resolver.resolve();
}
```

### 第 2 步：在 dispatcher 里填输入，不要加新的 `if` 分支

文件：`adapters/eldenring/src/input/hotkey_dispatcher.cpp`

```cpp
control_inputs.aim_mode = mode.aim_mode.load(std::memory_order_relaxed);
const mc::ControlPlan control_plan = eldenring::control::buildControlPlan(control_inputs);
mode.control_authority_bits.store(control_plan.packedAuthorities(), std::memory_order_relaxed);
```

按键屏蔽和鼠标/滚轮抑制已经由 plan 自动应用，不需要再写
`SetMaskedKeys` 或 `SetSuppressMouseButtons`。

### 第 3 步：消费 authority

- 相机/移动/跳跃：`ctx.mode().modOwns(mc::ControlDomain::Camera)`、
  `ctx.mode().blocksNative(mc::ControlDomain::Jump)`。
- 需要新的执行动作时，在 `hotkey_dispatcher` 里把 plan 翻译成宿主调用。
- 不要直接读 `first_person` 之类的旧 flag 来做新判断；旧 flag 只保留给
  渲染/HUD 等确实需要“第一人称外观”的地方。

### 第 4 步：写测试

在 `tests/test_eldenring_control_policy.cpp` 里加一个纯逻辑测试：

```cpp
TEST(EldenRingControlPolicy, AimModeOwnsCameraAndFreezesJump) {
    ModeInputs in;
    in.aim_mode = true;

    const mc::ControlPlan plan = buildControlPlan(in);
    EXPECT_TRUE(plan.owns(mc::ControlDomain::Camera));
    EXPECT_TRUE(plan.blocksNative(mc::ControlDomain::Jump));
}
```

## 5. 新增一个 Domain

如果现有 16 个 domain 不够用：

1. 在 `include/mc/control_policy.hpp` 的 `ControlDomain` 里**末尾**追加，
   不要改已有值（打包位是 2 bit/domain）。
2. 更新 `controlDomainName`。
3. 在 `ControlPlan` 里按需加便捷方法（可选）。
4. 在适配器 builder 里声明，在消费端读取。
5. 加测试；跑全量 `mc_tests`。

## 6. 接新游戏时 adapter 要做什么

core 不关心宿主怎么屏蔽输入。新适配器只需要：

1. 实现一个 `buildControlPlan(const ModeInputs&)` 等价的纯函数
   （模式 → `ControlRequest`）。
2. 把 `ControlPlan` 翻译成宿主的执行原语：
   - DirectInput：`SetMaskedKeys` / `SetSuppressMouseButtons` / `SetSuppressMouseWheel`；
   - 相机：把相机位置写到眼睛处；
   - 移动/跳跃：写速度、写位置、或调用宿主的移动接口。
3. 把 `packedAuthorities()` 存到一个跨线程可读的 atomic 里，供游戏线程
   和渲染线程查询。
4. 不要改 `include/mc/` 去适配某一个游戏；契约改动必须对所有游戏成立，
   并且有测试、所有 adapter 同步。

> **mod 插件如何贡献策略**：目前 `ModeInputs` 由适配器填写。如果以后要让
> `IModInstance` 也参与仲裁，给 `IModInstance` 加一个
> `onControlPolicy(ControlResolver&)`，由 `ModManager` 在 `resolve()` 前转发。
> 不要让 mod 直接改 `ModeConfig` 或自己调用 `SetMaskedKeys`。

## 7. 测试与验证

```powershell
# Windows / MSVC
cmake --build build --config Release --target mc_tests
.\build\bin\Release\mc_tests.exe --gtest_brief=1

# 只跑策略层
.\build\bin\Release\mc_tests.exe --gtest_filter="ControlPolicy*:EldenRingControlPolicy*"
```

当前覆盖：

- `tests/test_control_policy.cpp`：priority、同优先级覆盖、单键去重/释放、
  packed 往返、容量上限。
- `tests/test_eldenring_control_policy.cpp`：第一人称、MC 战斗、MC 跳跃、
  翻滚、飞行、CS 按键/滚轮、背包、前后台、组合模式。
- `tests/test_eldenring_hotkey_dispatcher.cpp`：真实 dispatcher 集成——
  MC 跳跃屏蔽空格、禁用翻滚屏蔽 Shift、创造飞行屏蔽移动键、CS 模式屏蔽
  R/F/1-4 与滚轮、背包打开时屏蔽鼠标移动/键盘。
- `tests/test_eldenring_inventory_controller.cpp`：背包开关/UI 逻辑
  （宿主输入抑制由 dispatcher 的 plan 测试覆盖）。

## 8. 反模式（代码审查直接打回）

- ❌ 在 `hotkey_dispatcher.cpp` 的按键屏蔽块里再加一个
  `if (mode.xxx) hidden[n++] = ...`。
- ❌ 在 `player_locomotion.cpp` 里再写
  `if (ctx.mode().first_person.load())` 决定相机/朝向。
- ❌ 在 `loader.cpp` 的相机写入里再写一个模式 flag 判断。
- ❌ 给 `ModeConfig` 加一个孤立 flag，却不给它对应的 `ControlRequest`。
- ❌ 用 `ControlAuthority::Shared` 表达“游戏不许动”（应该用 `Mod` 或 `None`）。
- ❌ 在 core 里写某个游戏的键码/偏移/模式名。

## 9. 文件地图

| 文件 | 职责 |
|---|---|
| `include/mc/control_policy.hpp` | 平台无关的 domain/authority/request/plan/resolver |
| `adapters/eldenring/include/eldenring_control_policy.hpp` | Elden Ring 模式 → request 的纯逻辑 |
| `adapters/eldenring/src/input/hotkey_dispatcher.cpp` | 每 tick 构建 plan，应用键屏蔽/鼠标/滚轮 |
| `adapters/eldenring/include/eldenring_runtime_context.hpp` | 用 packed atomic 发布 plan，提供 `modOwns` / `blocksNative` |
| `adapters/eldenring/src/movement/player_locomotion.cpp` | 用 `Camera` authority 决定身体朝向 |
| `adapters/eldenring/src/ui/inventory_controller.cpp` | 只维护背包开关/UI；宿主输入抑制由 dispatcher 的 plan 应用 |
| `adapters/eldenring/src/loader.cpp` | 用 `Camera` authority 决定是否写第一人称相机 |
| `tests/test_control_policy.cpp` | core 策略测试 |
| `tests/test_eldenring_control_policy.cpp` | Elden Ring 模式矩阵测试 |

## 10. 迁移记录（本次）

- 新增 `mc::ControlResolver` / `ControlPlan` / `ControlAuthority`。
- `hotkey_dispatcher.cpp` 的按键屏蔽 if-chain 改为
  `buildControlPlan` → `ControlPlan::masked_keys`。
- 鼠标键/滚轮抑制改为 `ControlPlan::blocksNative(...)`。
- 第一人称相机/移动判断从 `first_person` flag 改为
  `ControlDomain::Camera` authority。
- `ModeConfig` 增加 `control_authority_bits`，跨线程无锁读。
- 全量 C++ 测试从 1161 增加到 1186（新增 25 项策略/集成测试）。
- 后续（2026-10-19）：背包的鼠标移动/键盘抑制迁入 `requestInventory`，由
  dispatcher 统一应用；CS2 构建 1188 tests，纯 MC 构建 1130 tests。
