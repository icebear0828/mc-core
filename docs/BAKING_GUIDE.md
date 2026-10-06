# 跨引擎资产烘焙与打包完整指南 (Asset Baking Guide)

本文档指导如何将 `assets/source/` 目录中的标准 3D 模型（`.obj` / `.mtl`）与贴图（`.png`）一键转换为各大私有与商业引擎的专属运行时资产包。

---

## 1. 源码资产清单 (`assets/source/`)

运行以下命令即可生成全套标准 3D 资产：
```bash
uv run tools/generate_source_assets.py --out-dir assets/source
```

产出物包括：
* **Steve 12 刚体部件**：`steve_head.obj`, `steve_body.obj`, `steve_right_arm.obj`, `steve_left_arm.obj`, `steve_right_leg.obj`, `steve_left_leg.obj` 及外层衣服。
* **标准 100cm 体素方块**：`dirt.obj`, `stone.obj`, `tnt.obj`。
* **道具与工具**：`diamond_sword.obj`, `diamond_pickaxe.obj`。
* **点阵材质贴图**：`steve.png` (64x64), `dirt.png` (16x16), `stone.png` (16x16), `tnt.png` (16x16) 等。

---

## 2. 各引擎一键封包实操

运行自动化打包入口脚本：
```bash
uv run tools/bake_assets.py --target all --output-dir assets/cooked
```

### 2.1 虚幻引擎 5 (UE5 / UE4) $\to$ `mc_assets_P.pak`
* **工具依赖**：`repak` (纯 Rust 开源命令行小工具，跨平台)
  ```bash
  cargo install repak
  ```
* **一键打包**：
  ```bash
  repak pack assets/source assets/cooked/ue5/mc_assets_P.pak
  ```
* **安装方法**：将生成的 `mc_assets_P.pak` 直接复制进游戏目录 `Content/Paks/~mods/` 即可生效。

### 2.2 赛博朋克 2077 (REDengine 4) $\to$ `mc_assets.archive`
* **工具依赖**：`WolvenKit CLI` (`wk-cli`)
  [下载 Release](https://github.com/WolvenKit/WolvenKit/releases)
* **一键打包**：
  ```cmd
  wk-cli pack -i assets/source -o assets/cooked/cp2077/mc_assets.archive
  ```
* **安装方法**：将 `mc_assets.archive` 复制到 `Cyberpunk 2077/archive/pc/mod/`。

### 2.3 GTA V (RAGE) $\to$ `mc_assets.rpf` / `.ydr`
* **工具依赖**：Blender + Sollumz 插件 / OpenIV
* **流程**：
  1. 使用 Sollumz 插件批量导入 `assets/source/models/*.obj`。
  2. 导出为 RAGE 原生 `.ydr` (YDrawable) 与 `.ytd` (YTextureDictionary)。
  3. 用 OpenIV 打包为 `mc_assets.rpf` 放入 `mods/update/x64/dlcpacks/`。

### 2.4 艾尔登法环 (Dantelion) $\to$ `.flver`
* **工具依赖**：`witchyBND` 或 `FLVER Editor`
* **流程**：
  将 OBJ 拖入工具直接转换为 `.flver` 格式并打包入 `mod/parts/`。

---

## 3. 发布与分发规则 (Pre-cooked Releases)

为了让终端玩家开箱即用，我们在 GitHub Releases 中直接提供已烘焙完成的预制包：
* 📦 `mc-assets-ue5-v1.0.zip`
* 📦 `mc-assets-cp2077-v1.0.zip`
* 📦 `mc-assets-gta5-v1.0.zip`

用户只需下载对应游戏的 Mod DLL 配合该引擎的资源包放入游戏即可畅玩！
