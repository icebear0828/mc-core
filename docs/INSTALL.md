# 玩家安装与使用完整指南 (Installation & Usage Guide)

本文档面向所有使用 `mc-core` 系列模组的玩家与开发者。只需简单两步（放置 Mod 插件 + 放置资产包），即可在各大游戏中体验正宗的 Minecraft 玩法！

---

## 🚀 核心安装概念（只需两样东西）

任何支持本项目的游戏，安装都由两部分组成：
1. **Mod 插件文件**（`.dll` 或 `.asi`）：负责数学计算、按键响应与游戏行为逻辑。
2. **资产包**（`.pak` / `.archive` / `.rpf` / `.bnd`）：负责 Steve 3D 模型、方块与贴图资源。

---

## 📂 各大游戏安装路径速查表

找到你的游戏安装根目录（即含有游戏启动 `.exe` 的目录），按表格对号入座放入对应文件：

### 1. 《黑神话：悟空》及虚幻 5 游戏 (Unreal Engine 5)
* **前置要求**：安装社区标准加载器 [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)。
* **文件放置路径**：
  | 模组文件 | 目标放置路径 |
  |---|---|
  | **插件库** `wukong_adapter.dll` | `游戏根目录/b1/Binaries/Win64/ue4ss/Mods/mc_core/` |
  | **资产包** `mc_assets_P.pak` | `游戏根目录/b1/Content/Paks/~mods/`（若无 `~mods` 文件夹则新建一个） |

---

### 2. 《赛博朋克 2077》 (REDengine 4)
* **前置要求**：安装社区框架 [Cyber Engine Tweaks (CET)](https://www.nexusmods.com/cyberpunk2077/mods/107)。
* **文件放置路径**：
  | 模组文件 | 目标放置路径 |
  |---|---|
  | **插件库** `cp2077_adapter.dll` | `游戏根目录/bin/x64/plugins/cyber_engine_tweaks/mods/mc_core/` |
  | **资产包** `mc_assets.archive` | `游戏根目录/archive/pc/mod/` |

---

### 3. 《GTA V》 (RAGE Engine)
* **前置要求**：安装 [ScriptHookV](http://www.dev-c.com/gtav/scripthookv/)（将 `ScriptHookV.dll` 与 `dinput8.dll` 放入游戏根目录）。
* **文件放置路径**：
  | 模组文件 | 目标放置路径 |
  |---|---|
  | **插件库** `gta5_adapter.asi` | `游戏根目录/` 直接放在游戏根目录下 |
  | **资产包** `mc_assets.rpf` | 使用 OpenIV 拖拽安装至 `游戏根目录/mods/update/x64/dlcpacks/mc_assets/` |

---

### 4. 《艾尔登法环》 (Dantelion Engine)
* **前置要求**：安装社区工具 [ModEngine2](https://github.com/soulsmods/ModEngine2)。
* **文件放置路径**：
  | 模组文件 | 目标放置路径 |
  |---|---|
  | **插件库** `elden_adapter.dll` | `modengine2/mod/dll/`（并在 `config_eldenring.toml` 中配置加载） |
  | **资产包** `parts/` 模型目录 | `modengine2/mod/parts/` |

---

### 5. 《生化危机》系列 (RE Engine)
* **前置要求**：安装 [REFramework](https://github.com/cursey/reframework)。
* **文件放置路径**：
  | 模组文件 | 目标放置路径 |
  |---|---|
  | **插件库** `re_adapter.dll` | `游戏根目录/reframework/plugins/` |
  | **资产包** 整合 Zip 包 | 直接拖入 [Fluffy Mod Manager](https://fluffypuffy.fav.cc/) 一键安装启用 |

---

## 🎮 游戏内默认按键操作

进入游戏后，默认操作体系如下：

| 操作指令 | 默认按键 / 鼠标输入 | 功能说明 |
|---|---|---|
| **进入／解除 Steve 变身** | **F4**（或小键盘 **9**） | 隐藏主角原模型，挂载 12 部位方块人并接管走跑跳跃 |
| **攻击 / 挖掘方块** | **鼠标左键** | 手持武器时攻击（下落时触发暴击）；对准已放置方块长按进行挖掘（阶段 0~9） |
| **放置方块 / 使用道具** | **鼠标右键** | 手持方块时对准 5 米内地面或墙面放置（自动对齐 100cm 栅格）；手持珍珠/弓箭时使用 |
| **切换手持物品** | **鼠标滚轮** 或 **数字键 1–9** | 在泥土、石头、TNT、钻石剑、钻石镐、末影珍珠之间轮换 |
| **潜行 (Sneak)** | **左 Shift** | Steve 身体前倾下蹲，防止边缘坠落 |
| **疾跑 (Sprint)** | **左 Ctrl** 或 **双击 W** | 提升移动速度至疾跑频率（手臂和双腿高频摆动） |
| **视角切换** | **F5** | 在第一人称与第三人称视角之间切换 |

---

## ❓ 常见问题排查 (FAQ)

### Q1: 进入游戏按 F4 变身后，主角模型隐身了，但看不到方块 Steve？
* **原因**：资产包（`.pak` / `.archive` / `.rpf`）没有放置到正确的目录，导致游戏引擎找不到 3D 模型。
* **解决**：检查上方表格中的**资产包目标放置路径**，确保文件后缀完全一致，重新启动游戏。

### Q2: 为什么右键放方块没有任何反应？
* **原因**：
  1. 视线所对准的表面距离超过了 **5 米**（MC 默认交互距离）。
  2. 方块放置的栅格与你当前站立的人物碰撞体发生重叠（防自卡死安全机制）。
* **解决**：靠近目标表面（约 2~3 米处），避免对准自己脚下直接放置。

### Q3: 为什么开枪或挥刀时，游戏原版动作和方块动作同时触发？
* **原因**：部分游戏（如 2077）在特定输入模式下未完全屏蔽原武器攻击。
* **解决**：在手持方块状态下，插件会自动挂起原生攻击事件；若有冲突，可在适配器配置文件中开启“强制独占输入模式”（`ExclusiveInput=1`）。
