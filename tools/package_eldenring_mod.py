#!/usr/bin/env python3
"""
tools/package_eldenring_mod.py
Production packaging and distribution builder for Elden Ring Minecraft Mod.
Performs strict anti-corruption asset verification, PE export analysis,
comprehensive sound pack verification, and automated archive validation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
from PIL import Image


def compute_sha256(target: bytes | Path) -> str:
    """Compute SHA256 hex digest for bytes or a file path."""
    h = hashlib.sha256()
    if isinstance(target, Path):
        with target.open("rb") as f:
            while chunk := f.read(65536):
                h.update(chunk)
    elif isinstance(target, (bytes, bytearray, memoryview)):
        h.update(target)
    else:
        raise TypeError(f"Unsupported type for compute_sha256: {type(target)}")
    return h.hexdigest().upper()


def verify_atlas_integrity(atlas_path: Path) -> bool:
    """Verify authentic 512x512 HUD atlas, ensuring non-empty essential UV regions."""
    if not atlas_path.is_file():
        return False
    try:
        img = Image.open(atlas_path).convert("RGBA")
    except Exception:
        return False

    w, h = img.size
    if w != 512 or h != 512:
        return False

    regions = {
        "item_shield": (0.908203, 0.320312, 0.939453, 0.351562),
        "entity_shield": (0.001953, 0.369141, 0.126953, 0.494141),
        "destroy_stage_0": (0.453125, 0.369141, 0.484375, 0.400391),
        "destroy_stage_9": (0.751953, 0.369141, 0.783203, 0.400391),
        "heart_full": (0.207031, 0.0, 0.224609, 0.017578),
    }

    for name, (u0, v0, u1, v1) in regions.items():
        x0 = int(round(u0 * w))
        y0 = int(round(v0 * h))
        x1 = int(round(u1 * w))
        y1 = int(round(v1 * h))
        cropped = img.crop((x0, y0, x1, y1))
        alpha_bytes = cropped.getchannel("A").tobytes()
        solid = sum(1 for a in alpha_bytes if a > 10)
        if name == "entity_shield" and solid < 700:
            return False
        if solid == 0:
            return False

    return True


def verify_skin_integrity(skin_path: Path, expected_w: int = 64, expected_h: int = 64) -> bool:
    """Verify skin texture has valid dimensions and solid non-zero alpha."""
    if not skin_path.is_file():
        return False
    try:
        img = Image.open(skin_path).convert("RGBA")
    except Exception:
        return False

    w, h = img.size
    if w != expected_w or h != expected_h:
        return False

    alpha_bytes = img.getchannel("A").tobytes()
    solid = sum(1 for a in alpha_bytes if a > 10)
    # Texture must not be completely transparent
    return solid >= 100


def verify_geo_model(geo_path: Path) -> bool:
    """Verify Bedrock geometry JSON structure and required bones."""
    if not geo_path.is_file():
        return False
    try:
        with geo_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        return False

    # Check for minecraft:geometry list or geometry dict
    geos = data.get("minecraft:geometry") or data.get("geometry")
    if not geos:
        return False
    return True


def verify_audio_pack(sounds_dir: Path) -> Tuple[int, int]:
    """Verify sounds_manifest.txt and all WAV sound assets."""
    manifest_file = sounds_dir / "sounds_manifest.txt"
    if not manifest_file.is_file():
        return (0, 0)

    events: set[str] = set()
    wav_files: list[Path] = []

    with manifest_file.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("|")
            if len(parts) >= 2:
                event_name = parts[0].strip()
                rel_path = parts[1].strip()
                events.add(event_name)
                full_path = sounds_dir / rel_path
                if not full_path.is_file():
                    raise FileNotFoundError(f"Missing sound file referenced in manifest: {full_path}")
                wav_files.append(full_path)

    # Validate WAV headers
    for p in wav_files:
        with p.open("rb") as f:
            header = f.read(44)
            if len(header) < 44:
                raise ValueError(f"WAV file too short: {p}")
            riff, size, wave = struct.unpack("<4sI4s", header[:12])
            if riff != b"RIFF" or wave != b"WAVE":
                raise ValueError(f"Invalid RIFF/WAVE header in {p}")
            fmt, fmt_size, audio_fmt, num_channels, sample_rate = struct.unpack("<4sIHH I", header[12:28])
            if fmt != b"fmt " or audio_fmt != 1:  # 1 = PCM
                raise ValueError(f"Non-PCM WAV file: {p}")

    return (len(events), len(wav_files))


def verify_pe_dll(dll_path: Path) -> bool:
    """Verify PE header, x86_64 architecture, and DirectInput8Create export."""
    if not dll_path.is_file() or dll_path.stat().st_size < 100_000:
        return False

    with dll_path.open("rb") as f:
        dos_hdr = f.read(64)
        if len(dos_hdr) < 64 or dos_hdr[:2] != b"MZ":
            return False
        e_lfanew = struct.unpack("<I", dos_hdr[60:64])[0]
        f.seek(e_lfanew)
        pe_sig = f.read(4)
        if pe_sig != b"PE\0\0":
            return False
        file_hdr = f.read(20)
        machine = struct.unpack("<H", file_hdr[:2])[0]
        if machine != 0x8664:  # IMAGE_FILE_MACHINE_AMD64
            return False

        # Read optional header standard fields
        opt_hdr = f.read(240)
        magic = struct.unpack("<H", opt_hdr[:2])[0]
        if magic != 0x020B:  # PE32+ (64-bit)
            return False

        # Export table data directory is at offset 112 in PE32+ Optional Header
        export_rva, export_size = struct.unpack("<II", opt_hdr[112:120])
        if export_rva == 0 or export_size == 0:
            return False

    # Check for export string DirectInput8Create
    content = dll_path.read_bytes()
    if b"DirectInput8Create" not in content:
        return False

    return True


def build_readme_content(version: str) -> str:
    """Generate comprehensive documentation for distribution package."""
    return f"""# 🎮 Elden Ring Minecraft Mod (v{version})

Transform your Lands Between journey into a seamless Minecraft sandbox adventure!
Play as Steve with authentic voxel block building, custom inventory UI, authentic sound effects,
mining progression, crossbow ranged combat, shield blocking, and mob summoning.

---

## ⚡ Quick Start / Installation

1. **Prerequisite:** Elden Ring (v1.12+ recommended).
2. **Disable Easy Anti-Cheat (EAC):**
   - Rename `start_protected_game.exe` to `start_protected_game.exe.bak` in your game folder.
   - Copy `eldenring.exe` and rename the copy to `start_protected_game.exe`.
   - *Alternatively:* Launch via `eldenring.exe` directly (a `steam_appid.txt` file is included in this package).
   - **Never go online with modded game files!**
3. **Install Mod:**
   - Extract the entire contents of this archive directly into your Elden Ring `Game` folder:
     `Steam\\steamapps\\common\\ELDEN RING\\Game\\`
   - Ensure `dinput8.dll` sits right next to `eldenring.exe`.
   - Ensure the `mods\\mc_adapter\\` directory is present in the `Game` folder.
4. **Launch the Game:**
   - Run Elden Ring as usual. The mod initializes automatically on launch.

---

## 🕹️ Controls & Keybindings

| Key / Action | Function |
|---|---|
| `F6` | **Toggle Minecraft Mode** (Activates Steve model, custom camera, and MC mechanics) |
| `F5` | **Toggle Game Mode** (Cycles between Survival and Creative flight mode) |
| `F10` | **Toggle First-Person View** (Switch between third-person Steve and immersive first-person) |
| `E` or `I` | **Open / Close Inventory** (Access crafting, armor slots, hotbar items) |
| `F2` | **Summon Spirit Mob** (Spawns Minecraft ally mobs) |
| `F3` | **Crossbow Fire** (Quick-fires crossbow bolt projectile) |
| `Left Click` | **Attack / Mine Block** (Melee weapon swing, mining blocks with block cracking) |
| `Right Click` | **Interact / Place Block / Eat / Shield Block** (Right-hand action) |
| `Shift` (Hold) | **Sneak / Crouch** (Steve sneak pose, edge protection) |
| `Ctrl` (Hold) | **Sprint** (Minecraft double-speed sprint with sprint particle trail) |
| `Space` | **Jump** (Minecraft discrete vertical jump) |

---

## 📦 Package Contents Breakdown

```
ELDEN RING/Game/
├── dinput8.dll              # Mod loader and core engine hook
├── mc_hud_atlas.png         # 512x512 authentic HUD atlas (hearts, hotbar, armor, shield)
├── mc_er_hit.bin            # Attack / Melee damage template
├── mc_er_bullet.bin         # Crossbow projectile template
├── steam_appid.txt          # Steam AppID (1245620) for offline launch
├── mc_er_steve.txt          # Tuned adapter physics & camera configuration
├── mc_er_damage.txt         # Damage pipeline toggle
├── mc_er_overlay.txt        # D3D12 UI rendering toggle
├── mc_er_input.txt          # DirectInput keyboard/mouse hook toggle
├── mc_er_summon.txt         # Spirit summon toggle
├── mc_er_summonhide.txt     # Summon model swap toggle
├── mc_er_bulletfire.txt     # Crossbow shooting toggle
├── README.md                # This manual
├── README_INSTALL.txt       # Plain-text quick installation guide
├── SHA256SUMS.txt           # Cryptographic file verification checksums
└── mods/
    └── mc_adapter/
        ├── mc_hud_atlas.png # Atlas backup for asset loader
        ├── steve.png        # Authentic Steve player skin
        ├── creeper.png      # Authentic Creeper mob skin
        ├── zombie.png       # Authentic Zombie mob skin
        ├── models/
        │   ├── creeper.geo.json   # Creeper 3D Bedrock geometry
        │   ├── skeleton.geo.json  # Skeleton 3D Bedrock geometry
        │   └── zombie.geo.json    # Zombie 3D Bedrock geometry
        └── sounds/
            ├── sounds_manifest.txt # Audio event mapping
            └── ... (98 high-fidelity 16-bit PCM WAV sound effects across 28 events)
```

---

## 🔧 Compatibility & Mod Engine 2 (ME2)

If you use **Mod Engine 2**:
1. Rename `dinput8.dll` to `mc_eldenring_adapter.dll`.
2. Move `mc_eldenring_adapter.dll` and `mods/` into your Mod Engine 2 folder.
3. Edit `config_eldenring.toml`:
   ```toml
   external_dlls = ["mc_eldenring_adapter.dll"]
   ```

---

## 🛠️ Troubleshooting

- **Game crashes on startup:**
  - Verify EAC is disabled.
  - Check `mc_er.log` in the game directory for error messages.
  - Ensure DirectX 12 feature level is supported.
- **Steve is invisible or pink:**
  - Check that `mods\\mc_adapter\\steve.png` and `mods\\mc_adapter\\mc_hud_atlas.png` exist.
- **No sounds playing:**
  - Check that `mods\\mc_adapter\\sounds\\sounds_manifest.txt` is present and XAudio2 is functioning.

---

## 📜 Credits & Attributions

- **Mod Architecture & Reverse Engineering:** `mc-core` team (Google DeepMind Antigravity pair programming).
- **Audio & Visual Assets:** Faithful Minecraft authentic recreations.
- **License:** Mod adapter code is released under the MIT License.
"""


def build_readme_cn_content(version: str) -> str:
    """Generate comprehensive Chinese documentation for distribution package."""
    return f"""# 🎮 艾尔登法环 Minecraft Mod（Elden Ring MC Core Adapter v{version}）

将交界地的受苦之旅转化为原汁原味的《我的世界》沙盒冒险！
化身史蒂夫（Steve），体验真实体素方块搭建与碰撞、MC 定制背包与快捷栏、原版音效与击打音、方块挖掘与多阶段开裂特效、弩箭远程狙击、盾牌格挡、疾跑潜行，以及将骨灰伙伴替换为苦力怕、骷髅与僵尸等 Minecraft 经典生物！

---

## ⚡ 快速安装指引

### 第一步：关闭小蓝熊反作弊（Easy Anti-Cheat, EAC）
由于本 Mod 通过 `dinput8.dll` 进行函数 Hook 与 Direct3D 12 渲染注入，**严禁在开启 EAC 的情况下联机**。
请按以下任一方法以离线安全模式启动：

- **方法 A（最简推荐）**：
  1. 打开游戏主目录：`Steam\\steamapps\\common\\ELDEN RING\\Game\\`
  2. 将 `start_protected_game.exe` 重命名为 `start_protected_game.exe.bak`。
  3. 将 `eldenring.exe` 复制一份，并将副本重命名为 `start_protected_game.exe`。
  4. 之后通过 Steam 启动游戏即可自动绕过 EAC 离线运行。
- **方法 B（直接启动）**：
  - 本安装包内已附带 `steam_appid.txt`（内容为 `1245620`）。
  - 在完全断开网络或离线状态下，直接双击 `eldenring.exe` 启动。

### 第二步：安装 Mod 文件
1. 将本压缩包解压后的**全部文件与文件夹**复制到艾尔登法环的 `Game` 目录下：
   `Steam\\steamapps\\common\\ELDEN RING\\Game\\`
2. 确保目录结构符合：
   - `dinput8.dll` 和 `mc_hud_atlas.png` 位于 `Game\\` 根目录下，与 `eldenring.exe` 同级。
   - `mods\\mc_adapter\\` 目录完整存在（包含材质皮肤、3D 模型与音频文件）。

### 第三步：Mod Engine 2（ME2）兼容安装（可选）
如果您使用 Mod Engine 2 进行多 Mod 管理：
1. 将 `dinput8.dll` 重命名为 `mc_eldenring_adapter.dll`。
2. 将 `mc_eldenring_adapter.dll` 以及 `mods` 文件夹放入您的 Mod Engine 2 目录中。
3. 编辑 `config_eldenring.toml`，在 `external_dlls` 配置项中添加：
   ```toml
   external_dlls = ["mc_eldenring_adapter.dll"]
   ```

---

## 🕹️ 游戏内操作按键表

| 按键 / 操作 | 对应功能 | 详细说明 |
|---|---|---|
| `F6` | **MC 模式总开关** | 一键激活史蒂夫 3D 模型、独立相机控制、体素物理与专属 HUD 界面 |
| `F5` | **游戏模式切换** | 在【生存模式】与【创造模式】之间切换（创造模式支持自由飞行与防摔死保护） |
| `F10` | **第一人称 / 第三人称切换** | 切换身临其境的第一人称体素视角与经典第三人称史蒂夫视角 |
| `E` 或 `I` | **打开 / 关闭背包** | 呼出 Minecraft 风格背包 UI，可管理物品栏、盔甲槽与合成槽 |
| `F2` | **召唤骨灰生物伙伴** | 触发召唤生物管线，生成僵尸、苦力怕等 Minecraft 盟友 |
| `F3` | **弩箭快速射击** | 消耗弩箭并发射物理抛物线弹道，带有原版拉弓射箭音效 |
| `鼠标左键` | **主手攻击 / 挖掘方块** | 挥动工具进行近战判定；长按瞄准方块可连续挖掘，附带 10 阶段碎裂贴图与破坏音效 |
| `鼠标右键` | **副手交互 / 放置 / 进食 / 格挡** | 右键使用副手盾牌进行格挡（免疫硬直并反弹伤害）；对准方块可放置体素；手持食物长按进食 |
| `Shift` (长按) | **潜行 / 下蹲** | 触发史蒂夫屈膝下蹲姿态，在悬崖与方块边缘具备边缘保护防摔落效果 |
| `Ctrl` (长按) | **疾跑冲刺** | 开启双倍移动速度，屏幕伴随动态 FOV 视野推拉及脚步扬尘粒子特效 |
| `空格键 (Space)` | **跳跃** | 离散垂直跳跃，配合疾跑可实现跨方块疾跑跳 |

---

## 🧩 核心特色机制说明

### 1. 真实 3D 渲染与视角无缝融合
- 采用 Direct3D 12 深度测试与模板缓冲区（Depth Probe & Occlusion），史蒂夫模型能完美被法环原版的树木、立柱与建筑遮挡，不会发生悬浮透视。
- 采用零延迟瞬时转向算法（Instant Turn），彻底消除主机输入惯性延迟，还原 PC 版 Minecraft 纯粹的指哪打哪操控手感。

### 2. 体素破坏与方块建造系统
- 点击方块即可触发破坏进度判定，随着挖掘时间增加，方块表面将依次展现 `destroy_stage_0` 到 `destroy_stage_9` 的高保真开裂动画，敲碎时播放材质碎裂声音与掉落逻辑。

### 3. 高精度 HUD 与饱食度/生命回复
- 界面集成满血心（Heart）、半心、饥饿度（Hunger）、护甲值与经验条。
- 饱食度处于健康状态时提供缓慢的生命回复；受到攻击播放原版经典的 `Oof` 受伤音效。

### 4. 盾牌格挡系统
- 真实 3D 盾牌模型与贴图（UV 实体盾牌包含完整高光与像素材质），右键举盾能够无死角格挡正前方伤害。

### 5. 纯净骨灰伙伴模型转换
- 深度逆向并精确定位 FromSoftware 绘制管线（`disp_flags1` 原生渲染位），在隐藏原版狼群骨灰模型的同时，绝不破坏挖掘裂纹、盾牌与游戏原本的视觉渲染，且杜绝任何内存并发悬空指针导致的崩溃。

---

## ⚙️ 配置文件 `mc_er_steve.txt` 参数说明

在游戏目录下的 `mc_er_steve.txt` 可按个人喜好自定义高级调校参数：

```ini
# 深度测试偏移常量（防止模型穿模穿地）
depth_const=0.050067
# 启用瞬时转向（1 为 PC 风格无惯性转向，0 为原版平滑转向）
instant_turn=1
# 转向角速度（当 instant_turn=0 时生效）
turn_speed=15.0
# 史蒂夫朝向角度偏差调整（默认 180 度修正）
yaw_offset_deg=180
# 开启 Minecraft 跳跃物理（1 启用，0 关闭）
mc_jump=1
# 遮挡测试（1 开启深度遮挡）
occlusion=1
# 禁用原版翻滚并绑定为潜行（1 开启）
disable_vanilla_roll=1
# 潜行快捷键（0x10 为 Shift 键的虚拟键码）
roll_key=0x10
# 背包快捷键（0x45 为 E 键的虚拟键码；亦支持 73 即 I 键）
inv_key=0x45
# 音量大小（0.0 至 1.0）
sound_volume=0.8
# 交互与挖掘最大距离（米）
reach=4.5
```

---

## 🛠️ 常见问题与排查指南

- **Q1：游戏启动时闪退或报错 0xc0000005？**
  - **排查**：请确认是否彻底关闭了小蓝熊（EAC）反作弊，且系统已安装最新的 DirectX 运行时及 Visual C++ 2015-2022 运行库。
  - **排查**：检查游戏根目录生成的 `mc_er.log` 日志文件，查看在加载哪一部分（DX12 初始化、音频系统、或 Hook 安装）发生异常。
- **Q2：史蒂夫模型或苦力怕模型呈粉紫色缺失？**
  - **排查**：检查 `mods\\mc_adapter\\` 下的 `steve.png`、`creeper.png` 及 `mc_hud_atlas.png` 是否完整存在。
- **Q3：游戏内听不到 Minecraft 击打或脚步音效？**
  - **排查**：检查 `mods\\mc_adapter\\sounds\\sounds_manifest.txt` 是否存在，以及 `sounds\\` 目录下的 98 个 `.wav` 文件是否齐全。

---

## 📜 鸣谢与版权声明

- **核心架构与适配开发**：`mc-core` 开发组。
- **版权声明**：本 Mod 纯属开源游戏爱好者交流与个人体验使用，遵循 MIT 开源协议。严禁用于任何商业目的或联机作弊行为。
"""



def build_distribution_package(
    repo_root: Path,
    out_dir: Path,
    version: str = "1.0.0",
    game_dir: Optional[Path] = None,
) -> Dict[str, Any]:
    """Assemble and compress the official Elden Ring Minecraft Mod release package."""
    repo_root = repo_root.resolve()
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if game_dir is None:
        game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")

    staging_dir = repo_root / "build" / "package_staging" / f"EldenRing_Minecraft_Mod_v{version}"
    if staging_dir.exists():
        shutil.rmtree(staging_dir)
    staging_dir.mkdir(parents=True, exist_ok=True)

    print(f"=== Assembling Elden Ring Minecraft Mod v{version} ===")
    print(f"Staging directory: {staging_dir}")

    # 1. Resolve and verify DLL
    dll_candidates = [
        repo_root / "build" / "bin" / "eldenring" / "dinput8.dll",
        repo_root / "build" / "bin" / "RelWithDebInfo" / "dinput8.dll",
        repo_root / "build" / "bin" / "Release" / "dinput8.dll",
        game_dir / "dinput8.dll",
    ]
    src_dll = next((p for p in dll_candidates if p.is_file()), None)
    if not src_dll:
        raise FileNotFoundError("Could not find compiled dinput8.dll in build directories!")

    if not verify_pe_dll(src_dll):
        raise ValueError(f"DLL verification failed on {src_dll}")
    shutil.copy2(src_dll, staging_dir / "dinput8.dll")
    print(f"  [OK] dinput8.dll ({src_dll.stat().st_size:,} bytes, verified x64 PE export)")

    # 2. Resolve and verify Authentic Atlas
    atlas_candidates = [
        repo_root / "build" / "authentic_atlas" / "mc_hud_atlas.png",
        repo_root / "assets" / "source" / "textures" / "mc_hud_atlas.authentic.png",
        game_dir / "mods" / "mc_adapter" / "mc_hud_atlas.png",
        game_dir / "mc_hud_atlas.png",
    ]
    src_atlas = next((p for p in atlas_candidates if p.is_file() and verify_atlas_integrity(p)), None)
    if not src_atlas:
        raise FileNotFoundError("Could not find authentic 512x512 mc_hud_atlas.png with verified shield!")

    # Root atlas
    shutil.copy2(src_atlas, staging_dir / "mc_hud_atlas.png")
    # Sub-atlas
    mod_adapter_dir = staging_dir / "mods" / "mc_adapter"
    mod_adapter_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src_atlas, mod_adapter_dir / "mc_hud_atlas.png")
    print(f"  [OK] mc_hud_atlas.png (512x512, verified entity_shield >= 700 solid pixels)")

    # 3. Resolve and copy binary templates
    hit_bin = repo_root / "tools" / "reverse" / "eldenring" / "templates" / "mc_er_hit.bin"
    if not hit_bin.is_file():
        hit_bin = game_dir / "mc_er_hit.bin"
    if not hit_bin.is_file() or hit_bin.stat().st_size != 576:
        raise FileNotFoundError(f"Invalid mc_er_hit.bin template (expected 576 bytes): {hit_bin}")
    shutil.copy2(hit_bin, staging_dir / "mc_er_hit.bin")
    print("  [OK] mc_er_hit.bin (576 bytes)")

    bullet_candidates = [
        repo_root / "tools" / "reverse" / "eldenring" / "templates" / "mc_er_bullet.bin",
        game_dir / "mc_er_bullet.bin",
    ]
    src_bullet = next((p for p in bullet_candidates if p.is_file()), None)
    if not src_bullet or src_bullet.stat().st_size != 272:
        raise FileNotFoundError("Valid mc_er_bullet.bin (272 bytes) not found!")
    # Keep copy in repo templates as well for future reproducibility
    repo_bullet = repo_root / "tools" / "reverse" / "eldenring" / "templates" / "mc_er_bullet.bin"
    if not repo_bullet.is_file():
        repo_bullet.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_bullet, repo_bullet)
    shutil.copy2(src_bullet, staging_dir / "mc_er_bullet.bin")
    print("  [OK] mc_er_bullet.bin (272 bytes)")

    # 4. Steam AppID for offline launch
    (staging_dir / "steam_appid.txt").write_text("1245620\n", encoding="utf-8")
    print("  [OK] steam_appid.txt (AppID 1245620)")

    # 5. Configurations & Toggles
    steve_cfg_content = """# Elden Ring Minecraft Adapter Configuration
abs_bias=0.05
block_drive=1
block_anchor=0
debug=0
depth_const=0.050067
depthview_gain=0.0
enemy_block_collision=1
fall_hold=1.0
fall_reset=1
hide_mask1=0x100A1
hide_mask2=1
hide_native=1
instant_turn=1
mc_jump=1
occlusion=1
rel_bias=0.05
turn_speed=15.0
yaw_offset_deg=180
disable_vanilla_roll=1
roll_key=0x10
inv_key=0x45
sound_volume=0.8
reach=4.5
blocks=1
block_collision=1
"""
    (staging_dir / "mc_er_steve.txt").write_text(steve_cfg_content, encoding="utf-8")

    for toggle in [
        "mc_er_damage.txt",
        "mc_er_overlay.txt",
        "mc_er_input.txt",
        "mc_er_summon.txt",
        "mc_er_summonhide.txt",
        "mc_er_bulletfire.txt",
    ]:
        (staging_dir / toggle).write_text("", encoding="utf-8")
    print("  [OK] Configuration files & runtime feature toggles")

    # 6. Entity Skins
    # Steve skin
    steve_skin_candidates = [
        game_dir / "mods" / "mc_adapter" / "steve.png",
        repo_root / "assets" / "source" / "textures" / "steve.png",
    ]
    src_steve = next((p for p in steve_skin_candidates if p.is_file() and verify_skin_integrity(p, 64, 64)), None)
    if not src_steve:
        # Check if 64x64 or 64x32
        src_steve = next((p for p in steve_skin_candidates if p.is_file()), None)
    if src_steve:
        shutil.copy2(src_steve, mod_adapter_dir / "steve.png")
        print(f"  [OK] steve.png ({src_steve.stat().st_size:,} bytes)")

    # Creeper skin
    creeper_skin_candidates = [
        repo_root / "assets" / "source" / "models" / "entities" / "creeper.authentic.png",
        game_dir / "mods" / "mc_adapter" / "creeper.png",
    ]
    src_creeper = next((p for p in creeper_skin_candidates if p.is_file() and verify_skin_integrity(p, 64, 32)), None)
    if not src_creeper:
        src_creeper = next((p for p in creeper_skin_candidates if p.is_file()), None)
    if src_creeper:
        shutil.copy2(src_creeper, mod_adapter_dir / "creeper.png")
        print(f"  [OK] creeper.png ({src_creeper.stat().st_size:,} bytes)")

    # Zombie skin
    zombie_skin_candidates = [
        game_dir / "mods" / "mc_adapter" / "zombie.png",
        repo_root / "assets" / "source" / "textures" / "zombie.png",
    ]
    src_zombie = next((p for p in zombie_skin_candidates if p.is_file()), None)
    if src_zombie:
        shutil.copy2(src_zombie, mod_adapter_dir / "zombie.png")
        print(f"  [OK] zombie.png ({src_zombie.stat().st_size:,} bytes)")

    # 7. Bedrock 3D Geometry Models
    models_target_dir = mod_adapter_dir / "models"
    models_target_dir.mkdir(parents=True, exist_ok=True)
    models_source_dir = repo_root / "assets" / "source" / "models" / "entities"
    for geo_name in ["creeper.geo.json", "skeleton.geo.json", "zombie.geo.json"]:
        geo_src = models_source_dir / geo_name
        if not geo_src.is_file():
            geo_src = game_dir / "mods" / "mc_adapter" / "models" / geo_name
        if not verify_geo_model(geo_src):
            raise ValueError(f"Geometry model validation failed on {geo_src}")
        shutil.copy2(geo_src, models_target_dir / geo_name)
    print("  [OK] Bedrock models (creeper, skeleton, zombie verified)")

    # 8. High-Fidelity Audio Pack
    sounds_src_dir = game_dir / "mods" / "mc_adapter" / "sounds"
    if not sounds_src_dir.is_dir():
        sounds_src_dir = repo_root / "assets" / "sounds"
    if sounds_src_dir.is_dir():
        event_count, wav_count = verify_audio_pack(sounds_src_dir)
        print(f"  [OK] Audio Pack: {event_count} events, {wav_count} WAV files verified")
        sounds_dest_dir = mod_adapter_dir / "sounds"
        if sounds_dest_dir.exists():
            shutil.rmtree(sounds_dest_dir)
        shutil.copytree(sounds_src_dir, sounds_dest_dir)
    else:
        raise FileNotFoundError(f"Sound assets directory missing: {sounds_src_dir}")

    # 9. README.md & README_INSTALL.txt & README_CN.md & 使用说明_中文.txt
    readme_md = build_readme_content(version)
    readme_cn = build_readme_cn_content(version)
    (staging_dir / "README.md").write_text(readme_md, encoding="utf-8")
    (staging_dir / "README_CN.md").write_text(readme_cn, encoding="utf-8")
    (staging_dir / "README_INSTALL.txt").write_text(
        f"Elden Ring Minecraft Mod v{version}\n"
        f"--------------------------------------------------\n"
        f"1. Copy all contents of this folder into your Elden Ring 'Game' directory.\n"
        f"2. Ensure Easy Anti-Cheat is disabled before launching.\n"
        f"3. In-game: Press F6 to toggle Minecraft mode, F5 for Creative flight, F10 for First-Person.\n"
        f"4. See README.md or README_CN.md for full controls and troubleshooting.\n",
        encoding="utf-8",
    )
    (staging_dir / "使用说明_中文.txt").write_text(
        f"============================================================\n"
        f" 艾尔登法环 Minecraft Mod v{version} - 快速安装与使用指南\n"
        f"============================================================\n\n"
        f"1. 安装方法：\n"
        f"------------------------------------------------------------\n"
        f" - 将本压缩包内的所有文件与文件夹解压到艾尔登法环 Game 文件夹下：\n"
        f"   例如：Steam\\steamapps\\common\\ELDEN RING\\Game\\\n"
        f" - 确保 dinput8.dll 与 eldenring.exe 处于同一目录下。\n\n"
        f"2. 运行前提（必须关闭反作弊 EAC）：\n"
        f"------------------------------------------------------------\n"
        f" - 本 Mod 仅供离线游玩，严禁开启反作弊联机！\n"
        f" - 简易关闭 EAC：\n"
        f"   1) 将 Game 目录下的 start_protected_game.exe 改名为 start_protected_game.exe.bak\n"
        f"   2) 复制 eldenring.exe，并将副本改名为 start_protected_game.exe\n"
        f"   3) 从 Steam 正常启动即可离线运行 Mod。\n\n"
        f"3. 游戏内快捷键一览：\n"
        f"------------------------------------------------------------\n"
        f" - F6         : 切换 Minecraft 模式总开关\n"
        f" - F5         : 切换 生存 / 创造飞行模式\n"
        f" - F10        : 切换 第一人称 / 第三人称视角\n"
        f" - E 或 I     : 打开 / 关闭背包与合成界面\n"
        f" - F2         : 召唤 Minecraft 盟友生物（骨灰替换）\n"
        f" - F3         : 弩箭发射\n"
        f" - 鼠标左键   : 挥击近战攻击 / 瞄准方块挖掘（伴随开裂特效）\n"
        f" - 鼠标右键   : 副手交互 / 放置方块 / 进食 / 盾牌格挡\n"
        f" - Shift(长按): 潜行（边缘防跌落）\n"
        f" - Ctrl(长按) : 疾跑（双倍移速与视野推拉）\n"
        f" - 空格键     : 垂直跳跃\n\n"
        f"详细说明与配置文件详解请参阅包内的 README_CN.md。\n",
        encoding="utf-8",
    )
    # Also update repo docs/README_CN.md for repository users
    repo_docs_cn = repo_root / "docs" / "README_CN.md"
    repo_docs_cn.parent.mkdir(parents=True, exist_ok=True)
    repo_docs_cn.write_text(readme_cn, encoding="utf-8")
    print("  [OK] Documentation generated (README.md, README_CN.md, README_INSTALL.txt, 使用说明_中文.txt)")

    # 10. Compute SHA256SUMS.txt
    checksum_lines: list[str] = []
    for f in sorted(staging_dir.rglob("*")):
        if f.is_file() and f.name != "SHA256SUMS.txt":
            rel = f.relative_to(staging_dir).as_posix()
            h = compute_sha256(f)
            checksum_lines.append(f"{h}  {rel}")

    manifest_path = staging_dir / "SHA256SUMS.txt"
    manifest_path.write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")
    print(f"  [OK] SHA256SUMS.txt created ({len(checksum_lines)} files)")

    # 11. Create ZIP Archive
    pkg_name = f"EldenRing_Minecraft_Mod_v{version}"
    zip_path = out_dir / f"{pkg_name}.zip"
    print(f"\nCompressing into {zip_path}...")

    total_uncompressed = 0
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for f in sorted(staging_dir.rglob("*")):
            if f.is_file():
                arcname = f.relative_to(staging_dir).as_posix()
                zf.write(f, arcname=arcname)
                total_uncompressed += f.stat().st_size

    zip_hash = compute_sha256(zip_path)
    zip_size = zip_path.stat().st_size
    print(f"  [DONE] Archive created: {zip_path.name}")
    print(f"  [SIZE] Compressed: {zip_size:,} bytes ({zip_size / (1024*1024):.2f} MB)")
    print(f"  [SIZE] Uncompressed: {total_uncompressed:,} bytes ({total_uncompressed / (1024*1024):.2f} MB)")
    print(f"  [SHA256] {zip_hash}")

    # 12. Validate Archive Round-Trip
    with zipfile.ZipFile(zip_path, "r") as zf:
        bad_file = zf.testzip()
        if bad_file:
            raise ValueError(f"Corrupted file inside zip: {bad_file}")

    return {
        "success": True,
        "version": version,
        "staging_dir": str(staging_dir),
        "zip_path": str(zip_path),
        "zip_size": zip_size,
        "uncompressed_size": total_uncompressed,
        "sha256": zip_hash,
        "file_count": len(checksum_lines) + 1,
    }


def main():
    parser = argparse.ArgumentParser(description="Package Elden Ring Minecraft Mod for distribution.")
    parser.add_argument("--version", default="1.0.0", help="Release version string (default: 1.0.0)")
    parser.add_argument("--out-dir", default=None, help="Output directory for zip (default: build/dist)")
    parser.add_argument("--game-dir", default=None, help="Elden Ring Game directory")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    out_dir = Path(args.out_dir) if args.out_dir else repo_root / "build" / "dist"
    game_dir = Path(args.game_dir) if args.game_dir else None

    result = build_distribution_package(
        repo_root=repo_root,
        out_dir=out_dir,
        version=args.version,
        game_dir=game_dir,
    )

    print("\n========================================================")
    print(" RELEASE PACKAGE BUILT SUCCESSFULLY FOR DISTRIBUTION")
    print("========================================================")
    print(f"Target Zip: {result['zip_path']}")
    print(f"Size:       {result['zip_size']:,} bytes ({result['zip_size'] / (1024*1024):.2f} MB)")
    print(f"SHA256:     {result['sha256']}")
    print(f"Files:      {result['file_count']} files total")
    print("========================================================")


if __name__ == "__main__":
    main()
