"""Atlas packing and PNG encoding."""
from __future__ import annotations

import io

from PIL import Image


def pack_sprites(sprites: list[tuple[str, tuple[int, int]]],
                 sprite_for,
                 atlas_size: int = 512, pad: int = 1) -> tuple[Image.Image, dict[str, tuple[float, float, float, float]]]:
    """Pack sprites row by row into a square RGBA atlas.

    ``sprite_for(name, size)`` returns the image for one entry. A transparent pad
    separates neighbours so nearest-neighbour sampling never reads across a seam.
    """
    atlas = Image.new("RGBA", (atlas_size, atlas_size), (0, 0, 0, 0))
    uv_map: dict[str, tuple[float, float, float, float]] = {}

    x, y, row_h = pad, pad, 0
    for name, (w, h) in sprites:
        if x + w + pad > atlas_size:
            x, y, row_h = pad, y + row_h + pad, 0
        if y + h + pad > atlas_size:
            raise ValueError("HUD sprites do not fit in the atlas")

        sprite = sprite_for(name, (w, h))
        atlas.paste(sprite, (x, y))
        uv_map[name] = (x / atlas_size, y / atlas_size, (x + w) / atlas_size, (y + h) / atlas_size)

        x += w + pad
        row_h = max(row_h, h)

    return atlas, uv_map


def atlas_png(atlas: Image.Image) -> bytes:
    buf = io.BytesIO()
    atlas.save(buf, format="PNG")
    return buf.getvalue()
