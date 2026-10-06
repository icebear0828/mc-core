# MC-Core

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License: PolyForm Noncommercial](https://img.shields.io/badge/License-PolyForm%20Noncommercial-green.svg)](https://polyformproject.org/licenses/noncommercial/1.0.0/)
[![Tests](https://img.shields.io/badge/tests-8%20passed%20%7C%20100%25-brightgreen.svg)]()

> **跨私有引擎“万物MC行为”通用 C++20 中间件核心库**  
> Engine-Agnostic Minecraft Mechanics Middleware for Proprietary Game Engines (Cyberpunk 2077, GTA V, Resident Evil, Elden Ring).

---

## 📖 项目背景 (Background)

在以往的沙盒移植中，将 Minecraft 经典要素（方块建造、挖掘、钻石剑战斗、TNT 爆破、末影珍珠传送与 12 部位方块人动作）引入其他 3D 游戏时，往往深度绑定商业引擎特定的程序化网格组件与对象反射树。

当我们将目光投向各大采用**私有自研引擎**的 3A 大作（如《赛博朋克2077》REDengine 4、《GTA V》RAGE 引擎、《生化危机》RE Engine 以及《艾尔登法环》Dantelion 引擎）时，缺乏统一的运行时程序化网格与反射接口。

**`mc-core` 正是为打破引擎壁垒而生**：
它将 Minecraft 规则、纯数学动画、体素对齐与弹道逻辑完全封装为一个**零第三方引擎依赖的现代 C++20 静态库**，通过声明式纯虚接口（`Contracts`）将物理碰撞、视觉挂载与伤害结算委托给各游戏专属适配器插件。

---

## ⚡ 核心系统与特性 (Features)

### 1. 100% 原版质感：12 部位刚体纯数学 Steve 动画机 (`SteveAnimator`)
* **拒绝写实骨骼拉伸**：不把 Steve 强行蒙皮到写实人体的 8 头身骨架上。
* **原生角色隐形 + 12 刚体挂载**：原游戏主角网格设为隐藏，挂载 12 个独立无变形的长方体 Part。
* **纯数学公式驱动**：每 Tick 以三角函数直接计算走跑步频（`Cos(stride * 0.6662)`）、手臂呼吸摆动、视线俯仰以及挥击缓动（`1 - (1 - swing)^4`）。

### 2. 体素世界建造与挖掘状态机 (`VoxelWorld`)
* **100cm 标准体素网格**：自动量化世界坐标至 1 米整数栅格。
* **智能表面吸附**：根据视线碰撞法线计算向外延伸放置位置，集成玩家自身物理胶囊体防穿模检测。
* **10 阶挖掘裂纹状态机**：针对不同工具（钻石镐、空手）与方块硬度计算破损进度，按阶段输出 Crack 0~9 裂纹状态。

### 3. 高保真弹道物理步进 (`BallisticsEngine`)
* **重力与阻力模拟**：弓箭分段扫掠检测，模拟空气阻力衰减与下坠曲线。
* **末影珍珠安全传送**：计算飞行抛物线，撞击后做地面可立足性与空间容积校验。
* **忠诚三叉戟回飞**：命中后支持召回指令，生成平滑向心加速度向量飞回玩家手中。

### 4. 声明式战斗规则 (`CombatEngine`)
* **暴击判定**：下落攻击触发 1.5 倍伤害与强力物理冲量击退。
* **横扫之刃**：手持钻石剑且处于地面满冷却状态自动激活横扫判定。
* **Boss 动态平衡**：输出包含固定数值与最大生命值百分比（`max_hp_percent`）的结构化 `HitIntent`。

### 5. 标准化资产提取流水线 (`tools/extract_mc_assets.py`)
* Python 工具自动化解析 Minecraft Java 1.21.1 客户端 `client.jar`。
* 批量导出标准 Wavefront `.obj` 模型与点阵采样 `.png` 贴图，一键导入 3ds Max / Blender 转换为各引擎专属资产（GTA `.ydr`、2077 `.mesh`、法环 `.flver`）。

---

## 📂 仓库目录结构 (Repository Layout)

```
mc-core/
├── include/
│   └── mc/
│       ├── types.hpp              # 基础向量、四元数、体素坐标与物品定义
│       ├── animator.hpp           # 12 部位纯数学 Steve 姿态计算器
│       ├── voxel_world.hpp        # 100cm 体素网格吸附、放置与挖掘状态机
│       ├── ballistics.hpp         # 箭矢、末影珍珠、三叉戟弹道步进模拟
│       ├── combat.hpp             # 暴击、横扫之刃、HitIntent 计算规则
│       └── contracts/             # 宿主引擎适配器纯虚接口契约
│           ├── physics_adapter.hpp # 射线与动态物理碰撞体接口
│           ├── render_adapter.hpp  # 主角隐身与 12 部位网格挂载接口
│           ├── combat_adapter.hpp  # 原生伤害结算与布娃娃冲量接口
│           └── input_adapter.hpp   # 手持物与视角输入查询接口
├── src/                           # 核心逻辑实现代码
│   ├── animator.cpp
│   ├── voxel_world.cpp
│   ├── ballistics.cpp
│   └── combat.cpp
├── tests/                         # 单元测试套件 (GoogleTest & Pytest)
│   ├── test_animator.cpp
│   ├── test_voxel_world.cpp
│   ├── test_combat.cpp
│   ├── test_ballistics.cpp
│   └── test_extract_assets.py
├── tools/                         # 资产提取与转换工具
│   └── extract_mc_assets.py
├── docs/                          # 架构与规格文档
│   └── ARCHITECTURE.md
├── CMakeLists.txt                 # CMake 构建脚本 (C++20, 零警告策略)
├── CONTRIBUTING.md                # 代码风格与 Conventional Commits 规范
└── README.md
```

---

## 🛠️ 构建与测试 (Build & Test)

### 前置要求
- **CMake** 3.24+
- **C++20 兼容编译器** (Clang 15+, GCC 12+, MSVC 19.34+)
- **Python** 3.10+ & `uv` (用于资产工具与测试)

### 1. 编译 C++ 核心库与运行测试
```bash
# 配置 CMake (Release 模式)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 编译静态库与测试目标
cmake --build build --config Release

# 执行完整单元测试
ctest --test-dir build --output-on-failure
```

### 2. 运行 Python 资产工具测试
```bash
uv run --with pytest --with pillow pytest tests/test_extract_assets.py
```

---

## 🗺️ 路线图 (Roadmap)

- [x] **M1: 核心逻辑库与接口契约 (Core & Contracts)**
  - [x] 12 部位纯数学 Steve 摆臂与步态动画
  - [x] 100cm 体素栅格双向吸附与挖掘状态机
  - [x] 重力与阻力弹道模拟
  - [x] 暴击与横扫战斗规则
  - [x] 单元测试 100% 覆盖
- [x] **M2: 资产提取工具链 (Asset Pipeline)**
  - [x] Python 脚本解析并导出标准 12 部位 OBJ 与 100cm 方块网格
- [ ] **M3: GTA V 首发适配器 (ScriptHookV C++)**
  - [ ] 原生角色隐身与 12 个 `.ydr` 挂载
  - [ ] 原生射线检测与动态方块碰撞体注册
- [ ] **M4: 赛博朋克 2077 适配器 (CET / Redscript)**
  - [ ] WolvenKit 网格打包与伤害事件桥接
- [ ] **M5: 3D 空间音频 (miniaudio 集成)**
  - [ ] 跨平台直接播放 MC 原版音频并计算距离衰减与随机 Pitch

---

## 📄 许可协议 (License)

本项目采用 [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/) 许可，仅供非商业及学习研究使用。
Minecraft 游戏资产与音效版权均归 Mojang Studios / Microsoft 所有。
