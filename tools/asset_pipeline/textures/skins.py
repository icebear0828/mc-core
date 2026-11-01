"""Steve / mob skin extraction and the left-limb mirror fill."""
from __future__ import annotations

import io
import shutil
import zipfile
from pathlib import Path

from PIL import Image

from ..core.constants import _JAR_TEXTURES, _LIMB_BLOCKS, _MOB_SKINS


def export_steve_skin(client_jar: Path, out_dir: Path) -> Path:
    """Copy the real wide-arm Steve skin (64x64) out of a local client.jar as steve.png."""
    entry = _JAR_TEXTURES + "entity/player/wide/steve.png"
    with zipfile.ZipFile(client_jar, "r") as jar:
        if entry not in jar.namelist():
            raise FileNotFoundError(f"{entry} not found in {client_jar}")
        skin = Image.open(io.BytesIO(jar.read(entry))).convert("RGBA")
    if skin.size != (64, 64):
        raise ValueError(f"expected a 64x64 skin, got {skin.size}")
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "steve.png"
    skin.save(out)
    return out


def _limb_rects(u0: int, v0: int, d: int = 4, w: int = 4, h: int = 12) -> dict[str, tuple[int, int, int, int]]:
    """The six faces of a limb's texture block (x, y, width, height), Minecraft's cube UV layout."""
    return {
        "top": (u0 + d, v0, w, d),
        "bottom": (u0 + d + w, v0, w, d),
        "right": (u0, v0 + d, d, h),
        "front": (u0 + d, v0 + d, w, h),
        "left": (u0 + d + w, v0 + d, d, h),
        "back": (u0 + 2 * d + w, v0 + d, w, h),
    }


def mirror_empty_left_limbs(skin: Image.Image) -> Image.Image:
    """Old-layout skins (the zombie) leave the left arm and leg blocks empty because the model mirrors the right limbs.
    The Steve rig reads the modern layout, so fill each empty left block with the mirrored right one: every face flipped
    horizontally and the two side faces swapped. A block that already has pixels is left alone."""
    out = skin.copy()
    for (ru, rv), (lu, lv) in _LIMB_BLOCKS:
        left = _limb_rects(lu, lv)
        alpha = out.getchannel("A")
        if any(alpha.crop((x, y, x + w, y + h)).getbbox() is not None for x, y, w, h in left.values()):
            continue  # the block already has pixels (a modern-layout skin)
        right = _limb_rects(ru, rv)
        swap = {"right": "left", "left": "right"}
        for face, (lx, ly, lw, lh) in left.items():
            sx, sy, sw, sh = right[swap.get(face, face)]
            piece = out.crop((sx, sy, sx + sw, sy + sh)).transpose(Image.FLIP_LEFT_RIGHT)
            out.paste(piece, (lx, ly))
    return out


def export_mob_skin(client_jar: Path, out_dir: Path, mob: str) -> Path:
    """Copy a mob's skin out of a local client.jar as <mob>.png."""
    if mob not in _MOB_SKINS:
        raise ValueError(f"unknown mob {mob!r}; available mobs: {sorted(_MOB_SKINS)}")
    entry = _JAR_TEXTURES + _MOB_SKINS[mob]
    with zipfile.ZipFile(client_jar, "r") as jar:
        if entry not in jar.namelist():
            raise FileNotFoundError(f"{entry} not found in {client_jar}")
        skin = Image.open(io.BytesIO(jar.read(entry))).convert("RGBA")
    if skin.size != (64, 64) and skin.size != (64, 32):
        raise ValueError(f"expected a 64x64 or 64x32 skin, got {skin.size}")
    if skin.size == (64, 64):
        skin = mirror_empty_left_limbs(skin)
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{mob}.png"
    skin.save(out)
    return out


def export_entity_models(models_dir: Path, out_dir: Path) -> list[Path]:
    """Copy all entity geometry .geo.json files to out_dir."""
    out_dir.mkdir(parents=True, exist_ok=True)
    copied = []
    candidates = list(models_dir.glob("*.geo.json")) + list(models_dir.glob("entities/*.geo.json"))
    seen = set()
    for src in candidates:
        if src.name in seen:
            continue
        seen.add(src.name)
        dst = out_dir / src.name
        shutil.copy(src, dst)
        copied.append(dst)
    return copied
