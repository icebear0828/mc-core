# MC-Core 跨私有引擎架构设计规范 (Architecture Specification)

## 1. 架构目标与背景

将 Minecraft 的经典沙盒与战斗玩法引入各大 3A 游戏时，一直受限于各大游戏底层的引擎架构差异。在通用商业引擎中，开发者通常借由引擎内建的程序化网格组件与统一对象反射树来实现方块生成与物理放置；然而在《赛博朋克2077》（REDengine）、《GTA V》（RAGE）、《生化危机》（RE Engine）以及《艾尔登法环》（Dantelion）等私有引擎中，缺乏通用的运行时程序化网格生成能力与统一反射接口。

`mc-core` 通过**三层解耦架构**解决此问题：
1. **纯 C++20 核心逻辑层（Core Engine）**：全权接管 MC 规则数学（12 部位方块骨架摆动、100cm 体素栅格吸附、步进抛物线弹道、暴击与横扫规则、鞘翅滑翔、进食、HUD 布局）。
2. **编排层 `mc::Session`**：把所有引擎串成**一个 `tick(dt, InputSnapshot)`**。适配器只负责填 `InputSnapshot` 和实现端口，**不再各自手写 tick 编排**（旧设计里各游戏的 `plugin_entry` 复制粘贴同一份逻辑，必然漂移）。
3. **抽象接口契约层（Contracts）**：将宿主物理探测、模型挂载、受击逻辑与输入查询抽象为纯虚接口，由各大引擎插件独立实现。

> 接入新游戏的完整流程、逆向方法与踩坑见 [**PORTING_PLAYBOOK.md**](PORTING_PLAYBOOK.md)。

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

### 2.5 编排层 `Session`
- **一个 tick 的顺序**：切槽/滚轮 → 取相机与玩家状态（规范空间）→ 攻击冷却回充 → 视线射线 → 攻击/挖掘 → 使用（放方块、进食、拉弓、放烟花）→ 进食结算 → 鞘翅 → 挥击进度 → 动画 + 弹道。
- **目标选择**：命中自己的体素 → 挖掘/放置；命中**已注册的非方块实体** → 战斗；`EntityId::None` 与 `LocalPlayer` 永不作为攻击目标。
- **击退归核心**：`CombatEngine::executeHit` 在 `processHit` 返回 true 后**统一触发一次** `triggerStaggerOrRagdoll`，适配器的 `processHit` 只改状态（血量、死亡、躯干值）。
- **身体朝向**：`Session` 持有身体 yaw，头部 yaw 相对身体，超过 50° 身体才跟着转（Minecraft 行为）；通过 `setSteveRoot` 告诉适配器。
- **俯仰约定**：动画器沿用 Minecraft（正 = 低头），`Session` 内部由规范空间的"抬头为正"取反。
- **鞘翅**：滑翔期间每帧把鞘翅速度经 `setLinearVelocity` 写给 `LocalPlayer`；撞墙造成的动能伤害作为对 `LocalPlayer` 的平伤害走 `executeHit`。
- **`InputSnapshot::on_ground_is_estimate`**：宿主拿不到真实着地标志时，落地判断是估计值；滑翔中忽略它，着陆交给鞘翅的射线。

### 2.6 HUD 布局 `HudLayout`
整数 GUI 缩放（`max(1, round(h/360))`，1080p → 3）与原版排布：快捷栏 182×22 居中贴底、选中框 24×23、物品 16×16、红心 8 像素间距/9 像素大小（重叠 1 像素）、鸡腿从右往左并与快捷栏右缘齐平。纯数学，平台无关，由测试固定。

---

## 3. 跨引擎接口契约清单

| 接口类 | 核心方法 | 职责说明 |
|---|---|---|
| `IPhysicsAdapter` | `raycastWorld`, `createBlockCollider`, `applyLinearImpulse`, `setLinearVelocity` | 视线射线检测、动态方块碰撞体注册、**叠加**冲量、**替换**速度（鞘翅驱动玩家） |
| `IRenderAdapter` | `setNativePlayerVisible`, `setSteveRoot`, `updateStevePartTransforms`, `spawnBlockVisual`, `setBlockCrackStage` | 原身隐形、**Steve 根（脚位置+身体 yaw）**、12 部位姿态（相对根）、方块渲染与裂纹 |
| `ICombatAdapter` | `processHit`, `getMaxHealth`, `triggerStaggerOrRagdoll` | `processHit` **只改状态**；击退由核心触发一次 |
| `IInputAdapter` | `getEquippedMainHand`, `getCameraPosition/Forward`, `getPlayerPosition/Velocity` | 手持物、相机、玩家状态（**规范空间**） |

**实体身份** `EntityId`（强类型枚举）：`None = 0`、`LocalPlayer = 1`、其余由适配器分配 `>= 2`。`RaycastResult` 把 `hit_entity`（只放已注册实体）与 `hit_collider_handle`（只放我们放的方块）分开；宿主指针/句柄**不得**直接当作 `EntityId`。

**规范空间**：Z 向上、右手系、X 向前、Y 向左、单位厘米；`yaw` 0 = +X 逆时针为正。端口边界以内全是规范空间，适配器负责与宿主轴向/单位互转（位置与方向分开处理，旋转按反射修正）。详见 PORTING_PLAYBOOK §2。

---

## 4. 资产管线与格式映射

通过 `tools/extract_mc_assets.py` 自动化解析**本地**的 Minecraft Java 客户端 jar（1.21.x），生成标准 Wavefront `.obj` 与 `.png` 贴图。**真实 Mojang 贴图只在本地生成，不进仓库**；仓库内的 `assets/source/textures/*.png` 与内嵌图集是程序合成的占位图（见 PORTING_PLAYBOOK §8）：
- **GTA V (RAGE)**：通过 Sollumz / OpenIV 转换为 `.ydr` (YDrawable)。
- **赛博朋克 2077 (REDengine)**：通过 WolvenKit 转换为 `.mesh`。
- **生化危机 (RE Engine)**：通过 RE Mesh Tools 转换为 `.mesh`。
- **艾尔登法环 (Dantelion)**：通过 FLVER Editor 转换为 `.flver`。

---

## 5. 适配器工程实现避坑与准入规范

在实现任何特定宿主引擎适配器时，必须遵循：

- [**《适配器工程设计与避坑规范》(ADAPTER_SPECIFICATION.md)**](ADAPTER_SPECIFICATION.md)：图形管线生命周期、指针与隐身同步、HUD 保真、跨平台构建（C1–C15 核对清单）。
- [**《新游戏接入手册》(PORTING_PLAYBOOK.md)**](PORTING_PLAYBOOK.md)：分阶段流程、真机逆向方法论、绑定层规则、坐标/单位/手性、渲染与 HUD、踩坑清单、只狼案例。

要点：
1. **图形管线**：Flip Model 逐帧获取与释放 RTV，强制拦截 `ResizeBuffers`；HUD 绘制期间用**点采样**。
2. **绑定与写入**：特征码在**解密后的运行映像**里找，唯一命中且结构校验，**没有兜底对象**；只写有证据的单个字段并可还原；**永远不要把游戏内存 `reinterpret_cast` 成自己的 C++ 结构**。
3. **资产**：HUD 与 Steve 皮肤用本地 jar 生成的真贴图，代码里不得出现字符占位或手绘几何。
