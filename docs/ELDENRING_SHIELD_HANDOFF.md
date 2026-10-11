# 艾尔登法环 MC 移植模组 - 盾牌与副手系统交接文档 (Handover Document)

> **创建时间:** 2026-10-10  
> **当前分支:** `feat/shield-item-inventory-3d` (基于 `feat/offhand-ui-vanilla`)  
> **工作区目录:** `D:\code\universal-modder\mc-core`  
> **游戏运行目录:** `C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game`  
> **资源 Jar 路径:** `D:\game\sekiro\build\mc_client_1.21.8.jar`

---

## 一、当前进展概览 (Progress Overview)

| 批次编号 | 分支 / Commit | 功能描述 | 验收状态 |
| :--- | :--- | :--- | :--- |
| **批次 1~3** | `main` | 核心生存战斗、移动系统、弓箭发射与零延迟命中修复 | ✅ 游戏内实机验收通过 |
| **批次 4** | `feat/shield-offhand` | 正面 100° 格挡计算、`item.shield.block` 音效调度、图腾免死 | ✅ 逻辑与判定通过 |
| **批次 4.1** | `feat/offhand-ui-vanilla` | 原版副手 HUD 贴图 (`hotbar_offhand_left.png`)、图集扩展 | ✅ 单元测试与导出通过 |
| **批次 4.2** | `feat/shield-item-inventory-3d` | 背包副手槽、创造模式 Palette 盾牌物品、F 键主副手互换、初始手臂动画绑定 | ✅ 43 个单元测试通过，已部署 |

---

## 二、最新实机反馈与根因深度剖析 (Ground Truth & Root Cause)

用户实测批次 4.2 后的真实反馈：
> **“你目前盾牌是透明的，也没做贴合手臂的边框和3d效果”**

### 1. 为什么“盾牌是透明的”？
- **根因：** 之前在 `tools/extract_mc_assets.py` 图集映射中，将 `"item_shield"` 指向了 `gui/sprites/container/slot/shield.png`。
- **真相：** 该贴图并非盾牌物品本身，而是原版 Minecraft 容器界面副手槽位为空时的**半透明背景水印**（translucent slot watermark），其大部分像素 alpha 极低。当 `isHeldAsFlatSprite` 尝试从该像素进行体素挤压（Extrude）时，生成了一个几乎完全透明的薄片网格，导致在游戏内手持看似“隐形或透明”。

### 2. 为什么“没做贴合手臂的边框和 3D 效果”？
- **根因：** 之前盾牌被当作普通 2D 扁平物品（如苹果、种子）处理，走的是 `buildHeldItemMesh` 单像素挤出通道。
- **真相：** 在原版 Minecraft 中，盾牌从来不是扁平物品，而是一个由两部分组成的专用 3D 实体模型（`builtin/entity`），使用贴图 `assets/minecraft/textures/entity/shield_base_nopattern.png` (64×64)：
  1. **板身 (Plate):** 长方形盾面，尺寸 $12 \times 22 \times 2$ 像素（带木纹底板与凸起的铁包边）；
  2. **握把 (Handle):** 背面把手，尺寸 $2 \times 6 \times 6$ 像素，用于扣在手臂上。
- **贴合手臂需求：** 握把必须精准对准 Steve 的前臂（Arm），盾牌主板面向外侧；举盾（Blocking）时，手臂抬至胸前（Pitch 向上倾斜、Yaw 略微内收），盾牌正面严密护住身体躯干。

---

## 三、接续开发任务与实现路线图 (Implementation Roadmap)

### 步骤 1：真正盾牌 3D 模型与几何定义
1. 在 `mc_core`（如 `include/mc/shield_model.hpp` 与 `src/shield_model.cpp`）实现专有的 `buildShieldMesh(const HostBasis& basis)`：
   - **Plate 立方体：**
     - 原点与尺寸：宽 12px，高 22px，厚 2px（转换为模型空间尺寸）；
     - UV 映射自 `shield_base_nopattern.png` 的 Plate 展开区；
   - **Handle 立方体：**
     - 尺寸：宽 2px，高 6px，深 6px；
     - 紧贴 Plate 背面中央，供手臂穿过握持；
2. 提取并烘焙原版 `shield_base_nopattern.png` 贴图并加入渲染纹理，或者如果使用 HUD 图集，生成带完整实心木色和铁框的经典 16×16 预渲染图标以解决 GUI 与通用显示。

### 步骤 2：手臂贴合绑定矩阵 (Arm Attachment Matrix)
1. **握持变换矩阵 (Rest / Idle Pose):**
   - 当装备在左手（副手）时，盾牌 Handle 中心锁定在左臂下半部外侧；
   - 盾牌板面向左前方微斜，不阻挡正常行走视线；
2. **举盾变换矩阵 (Blocking Pose):**
   - 触发右键举盾（`anim_in.is_blocking == true`）时，`SteveAnimator` 驱动左臂旋转：
     - `pitch = pitch * 0.5f - kPi * 0.3f + head_pitch;`
     - `yaw = kPi / 6.0f + head_yaw;`
   - 盾牌板面随左臂矩阵抬起到玩家正前方，构成坚固的立体正面盾墙。

### 步骤 3：实心 GUI 图标优化
- 彻底替换 `gui/sprites/container/slot/shield.png` 作为物品图标的做法；
- 为 `ItemId::Shield` 提供真实原版外观的实心 16×16 GUI 图标（具有清晰的白/铁边框与棕色木质内衬），空副手槽才使用半透明的水印。

---

## 四、验证与操作指南 (Verification & Commands)

### 1. 运行单元测试
```powershell
# 运行全部盾牌、背包、手持模型测试
.\build\bin\Debug\mc_tests.exe --gtest_filter="Inventory.*:ShieldItem.*:InventoryLayout.*:ItemModelTest.*:EldenRingShield*"

# 运行 Python 图集资产测试
python -m pytest tests/test_extract_assets.py
```

### 2. 编译 Release DLL
```powershell
cmake --build build --config Release --target eldenring_adapter
```

### 3. 部署并验证
```powershell
python tools/reverse/eldenring/deploy_batch4_2.py
```
（部署脚本会自动备份游戏目录 `dinput8.dll` 并验证 SHA256 强一致性）

---

## 五、关键文件清单 (Key Code Locations)

- **盾牌手持模型与样式:** [`src/item_model.cpp`](file:///D:/code/universal-modder/mc-core/src/item_model.cpp)
- **Steve 渲染与副手物品绘制:** [`adapters/eldenring/src/steve_renderer_d3d12.cpp`](file:///D:/code/universal-modder/mc-core/adapters/eldenring/src/steve_renderer_d3d12.cpp#L880)
- **背包副手槽布局与命中判定:** [`include/mc/inventory_layout.hpp`](file:///D:/code/universal-modder/mc-core/include/mc/inventory_layout.hpp)
- **背包界面渲染:** [`adapters/eldenring/src/overlay_d3d12.cpp`](file:///D:/code/universal-modder/mc-core/adapters/eldenring/src/overlay_d3d12.cpp#L590)
- **输入控制与 F 键换手:** [`adapters/eldenring/src/loader.cpp`](file:///D:/code/universal-modder/mc-core/adapters/eldenring/src/loader.cpp#L2450)
