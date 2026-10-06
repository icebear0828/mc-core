# MC-Core

> 跨私有引擎“万物MC行为”通用 C++20 中间件核心库 (Engine-Agnostic Minecraft Mechanics Middleware)

`mc-core` 是从《黑神话：悟空》史蒂夫模组（`wukong-steve`）抽象提炼的纯 C++20 核心逻辑库。它将 Minecraft 标志性的 12 部位刚体方块人动画、体素方块世界放置与挖掘、抛物线弹道物理、战斗规则和独立 3D 空间音效封装为轻量、可单元测试、无引擎依赖的独立模块，支持无缝嵌入至各类私有引擎（如 GTA V / RAGE、赛博朋克 2077 / REDengine、生化危机 / RE Engine、艾尔登法环 / Dantelion 等）。

## 核心特性

- **100% MC 纯数学骨架动画**：采用 12 部位独立刚体长方体，通过纯三角函数公式计算步态与攻击缓动，避免写实人体骨骼对方块人的拉伸畸变。
- **100cm 体素世界交互**：支持视线射线检测、整数网格吸附、物理包围盒动态注册、以及 10 阶（Crack 0~9）方块挖掘状态机。
- **高保真弹道物理**：步进模拟箭矢重力衰减与扫掠检测、末影珍珠安全落地点验证与传送代价、忠诚三叉戟回飞引力曲线。
- **松耦合接口契约**：定义 `IPhysicsAdapter`、`IRenderAdapter`、`ICombatAdapter`、`IInputAdapter`，与宿主游戏完全解耦。

## 目录结构

```
mc-core/
├── include/
│   └── mc/
│       ├── types.hpp              # 基础向量、四元数、体素坐标与物品定义
│       ├── animator.hpp           # 12 部位纯数学 Steve 姿态计算器
│       ├── voxel_world.hpp        # 体素网格吸附、放置与挖掘状态机
│       ├── ballistics.hpp         # 箭矢、末影珍珠、三叉戟弹道模拟
│       ├── combat.hpp             # 暴击、横扫之刃、HitIntent 计算
│       └── contracts/             # 宿主引擎适配器纯虚接口契约
│           ├── physics_adapter.hpp
│           ├── render_adapter.hpp
│           ├── combat_adapter.hpp
│           └── input_adapter.hpp
├── src/                           # 核心逻辑实现
├── tests/                         # GoogleTest 单元测试
├── tools/                         # 资产提取与转换脚本 (Python)
├── docs/                          # 架构设计与各引擎适配指南
├── CMakeLists.txt
├── CONTRIBUTING.md
└── README.md
```

## 构建指南

### 前置要求
- CMake 3.24+
- 支持 C++20 的编译器 (Clang 15+, GCC 12+, MSVC 19.34+)

### 编译与运行测试
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```
