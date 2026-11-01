"""HUD atlas assembly: real client.jar sprites where available, canonical placeholders otherwise."""
from __future__ import annotations

import io
import zipfile
from pathlib import Path

from PIL import Image

from ..core.constants import (
    HUD_SPRITES,
    _CONTAINER_PARTS,
    _CONTAINER_TEXTURE,
    _JAR_BLOCK_ITEMS,
    _JAR_HUD_SPRITES,
    _JAR_TEXTURES,
    _SPAWN_EGG_COLOURS,
)
from ..textures.voxel_renderer import _multiply_tint, isometric_block_icon
from .builder import pack_sprites
from .canonical_sprites import create_canonical_sprite


def _open_jar_png(jar_zip: zipfile.ZipFile, relative: str) -> Image.Image | None:
    full = _JAR_TEXTURES + relative
    if full not in jar_zip.namelist():
        return None
    try:
        with jar_zip.open(full) as zf:
            return Image.open(io.BytesIO(zf.read())).convert("RGBA")
    except Exception:
        return None


def _fit_exact(src: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Sprites are used at their native size; anything else is centred (never stretched)."""
    if src.size == size:
        return src
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.paste(src, ((size[0] - src.width) // 2, (size[1] - src.height) // 2))
    return canvas


def _read_real_hud_sprite(jar_zip: zipfile.ZipFile, name: str, size: tuple[int, int]) -> Image.Image | None:
    """Real Minecraft sprite for `name`, or None so the caller falls back to the canonical placeholder."""
    if name in _JAR_BLOCK_ITEMS:
        top_path, side_path = _JAR_BLOCK_ITEMS[name]
        top, side = _open_jar_png(jar_zip, top_path), _open_jar_png(jar_zip, side_path)
        if top is None or side is None:
            return None
        return isometric_block_icon(top, side, size[0])
    if name in _CONTAINER_PARTS:
        src = _open_jar_png(jar_zip, _CONTAINER_TEXTURE)
        return None if src is None else src.crop(_CONTAINER_PARTS[name])
    if name in _SPAWN_EGG_COLOURS:
        own = _JAR_HUD_SPRITES.get(name)
        finished = _open_jar_png(jar_zip, own) if own else None
        if finished is not None:  # newer jars: the finished texture of the mob's egg
            return _fit_exact(finished, size)
        base, spots = _open_jar_png(jar_zip, "item/spawn_egg.png"), _open_jar_png(jar_zip, "item/spawn_egg_overlay.png")
        if base is None or spots is None:
            return None
        primary, secondary = _SPAWN_EGG_COLOURS[name]
        egg = _multiply_tint(_fit_exact(base, size), primary)
        egg.alpha_composite(_multiply_tint(_fit_exact(spots, size), secondary))
        return egg
    if name == "item_shield":
        # 1.21.8 has no item/shield.png (the inventory icon is the 3D model): use the front face of the plate in the entity texture.
        plate = _open_jar_png(jar_zip, "entity/shield_base_nopattern.png")
        if plate is None or plate.size != (64, 64):
            return None
        front = plate.crop((1, 1, 13, 23))  # 12x22, the face with the rivets
        scaled = front.resize((9, size[1]), Image.BOX)
        icon = Image.new("RGBA", size, (0, 0, 0, 0))
        icon.paste(scaled, ((size[0] - scaled.width) // 2, 0))
        return icon
    if name.startswith("destroy_stage_"):
        relative = _JAR_HUD_SPRITES.get(name)
        if relative is None:
            return None
        src = _open_jar_png(jar_zip, relative)
        if src is None:
            return None
        rgba = src.convert("RGBA")
        px = rgba.load()
        for y in range(rgba.height):
            for x in range(rgba.width):
                r, g, b, a = px[x, y]
                if a <= 1:
                    px[x, y] = (0, 0, 0, 0)
        return _fit_exact(rgba, size)
    relative = _JAR_HUD_SPRITES.get(name)
    if relative is None:
        return None
    src = _open_jar_png(jar_zip, relative)
    return None if src is None else _fit_exact(src, size)


def build_hud_atlas(client_jar: Path | None = None) -> tuple[Image.Image, dict[str, tuple[float, float, float, float]]]:
    """Build the 512x512 RGBA HUD atlas and return (image, uv_mapping). The layout never depends on
    whether a client.jar was given, so generated UV constants stay valid for either atlas."""
    jar_zip = None
    if client_jar and client_jar.is_file():
        try:
            jar_zip = zipfile.ZipFile(client_jar, "r")
        except Exception:
            jar_zip = None

    def sprite_for(name: str, size: tuple[int, int]) -> Image.Image:
        sprite = _read_real_hud_sprite(jar_zip, name, size) if jar_zip else None
        return sprite if sprite is not None else create_canonical_sprite(name)

    try:
        atlas, uv_map = pack_sprites(HUD_SPRITES, sprite_for)
    finally:
        if jar_zip:
            jar_zip.close()
    return atlas, uv_map
