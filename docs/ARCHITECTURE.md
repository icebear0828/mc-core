# MC-Core 跨私有引擎架构设计规范 (Architecture Specification)

## 1. 架构目标与背景

将 Minecraft 的经典沙盒与战斗玩法引入各大 3A 游戏时，一直受限于各大游戏底层的引擎架构差异。在通用商业引擎中，开发者通常借由引擎内建的程序化网格组件与统一对象反射树来实现方块生成与物理放置；然而在《赛博朋克2077》（REDengine）、《GTA V》（RAGE）、《生化危机》（RE Engine）以及《艾尔登法环》（Dantelion）等私有引擎中，缺乏通用的运行时程序化网格生成能力与统一反射接口。

`mc-core` 通过**双层解耦架构**解决此问题：
1. **纯 C++20 核心逻辑层（Core Engine）**：全权接管 MC 规则数学（12 部位方块骨架摆动、100cm 体素栅格吸附、步进抛物线弹道、暴击与横扫规则）。
2. **抽象接口契约层（Contracts）**：将宿主物理探测、模型挂载、受击逻辑与输入查询抽象为纯虚接口，由各大引擎插件独立实现。

---

## 2. 核心子系统设计

### 2.1 12 部位刚体纯数学 Steve 动画机 (`SteveAnimator`)
- **设计避坑**：严禁将方块人直接绑定到写实人体的 8 头身骨骼（否则手臂与腿部会产生严重的关节拉伸和扭曲）。
- **实现机理**：
  - 将宿主原主角网格设为不可见（`SetNativePlayerVisible(false)`），保留原角色的物理胶囊体用于场景移动。
  - 生成 12 个刚性立方体 Part（`head`, `body`, `right_arm`, `left_arm`, `right_leg`, `left_leg` 及外层衣服）。
  - 每帧直接计算 MC 官方步态数学公式：
    - 步频累加：$\text{stride} \mathrel{+}= \text{speed} \times \Delta t \times 4.0$
    - 双腿交替摆动：$\theta_{\text{leg}} = \cos(\text{stride} \times 0.6662 + \phi) \times 1.4 \times \text{amplitude}$
    - 手臂摆动与呼吸起伏：$\theta_{\text{arm}} = \cos(\text{stride} \times 0.6662 + \phi) \times \text{amplitude} \pm \sin(\text{age} \times 0.067) \times 0.05$
    - 挥击缓动：$\text{ease} = 1 - (1 - \text{swing})^4$

### 2.2 100cm 体素建造与挖掘状态机 (`VoxelWorld`)
- **网格吸附**：以 100cm（1 米）为体素量化单元。
- **表面判定**：
  $$\mathbf{P}_{\text{target}} = \mathbf{P}_{\text{impact}} + \mathbf{N}_{\text{impact}} \times 50.0$$
  通过四舍五入对齐至整数栅格，并校验该栅格是否与玩家当前站立胶囊体碰撞（防止自卡死）。
- **挖掘刻数**：针对不同材质（石头、泥土、TNT）配置基础破损时间，计算实时挖掘进度（0.0 ~ 1.0），按阶段触发 0~9 阶表面裂纹回调。

### 2.3 抛物线弹道步进 (`BallisticsEngine`)
- **空气阻力与重力积分**：
  $$\mathbf{v}_{z} \mathrel{-}= g \times \Delta t, \quad \mathbf{v} \mathrel{*}= k_{\text{drag}}$$
- **连续扫掠检测（Swept Raycast）**：防止高速箭矢（$60\text{ m/s}$）穿墙。
- **忠诚三叉戟（Loyalty Trident）**：掷出后检测手持召回指令，生成指向玩家手部的向心加速度向量平滑回飞。

### 2.4 声明式战斗规则 (`CombatEngine`)
- **暴击判定**：下落状态中命中敌人触发 $1.5\times$ 伤害与强击退冲量。
- **横扫之刃**：手持钻石剑且处于地面满冷却状态触发范围 AOE 横扫。
- **动态 Boss 平衡**：输出结构化 `HitIntent`，包含绝对伤害与最大生命值百分比（`max_hp_percent`），由宿主适配器灵活映射。

---

## 3. 跨引擎接口契约清单

| 接口类 | 核心方法 | 职责说明 |
|---|---|---|
| `IPhysicsAdapter` | `raycastWorld`, `createBlockCollider`, `applyLinearImpulse` | 视线射线检测、动态方块物理碰撞体注册、刚体受击推力 |
| `IRenderAdapter` | `setNativePlayerVisible`, `updateStevePartTransforms`, `spawnBlockVisual`, `setBlockCrackStage` | 原身隐形、12 部位方块姿态更新、方块渲染与裂纹贴图更新 |
| `ICombatAdapter` | `processHit`, `getMaxHealth`, `triggerStaggerOrRagdoll` | 原生受击系统转接、削韧、受击动作与布娃娃触发 |
| `IInputAdapter` | `getEquippedMainHand`, `getCameraPosition`, `getPlayerVelocity` | 检测玩家手持物、读取摄像机朝向与玩家移动速度 |

---

## 4. 资产管线与格式映射

通过 `tools/extract_mc_assets.py` 自动化解析 Minecraft Java 1.21.1 客户端，生成标准 Wavefront `.obj` 与 `.png` 贴图：
- **GTA V (RAGE)**：通过 Sollumz / OpenIV 转换为 `.ydr` (YDrawable)。
- **赛博朋克 2077 (REDengine)**：通过 WolvenKit 转换为 `.mesh`。
- **生化危机 (RE Engine)**：通过 RE Mesh Tools 转换为 `.mesh`。
- **艾尔登法环 (Dantelion)**：通过 FLVER Editor 转换为 `.flver`。
