"""2.5D block icon shading and cube mesh generation."""
from __future__ import annotations

from PIL import Image

from ..core.constants import _CUBE_SHADE


def _shade(img: Image.Image, factor: float) -> Image.Image:
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            px[x, y] = (int(r * factor), int(g * factor), int(b * factor), a)
    return out


def _multiply_tint(img: Image.Image, color: tuple[int, int, int]) -> Image.Image:
    """Colour * texel, per channel (how Minecraft tints a grey layer); alpha is kept."""
    out = img.convert("RGBA")
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = (r * color[0] // 255, g * color[1] // 255, b * color[2] // 255, a)
    return out


def _tint(img: Image.Image, color: tuple[int, int, int], amount: float) -> Image.Image:
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = tuple(int(c * (1 - amount) + t * amount) for c, t in zip((r, g, b), color)) + (a,)
    return out


def _paste_face(canvas: Image.Image, face: Image.Image, origin: tuple[float, float],
                u_axis: tuple[float, float], v_axis: tuple[float, float]) -> None:
    """Map the face texture's unit square onto the parallelogram origin + u*u_axis + v*v_axis."""
    (ux, uy), (vx, vy) = u_axis, v_axis
    det = ux * vy - uy * vx
    if det == 0:
        return
    size = face.width
    src = face.load()
    dst = canvas.load()
    for y in range(canvas.height):
        for x in range(canvas.width):
            dx, dy = x + 0.5 - origin[0], y + 0.5 - origin[1]
            u = (dx * vy - dy * vx) / det
            v = (ux * dy - uy * dx) / det
            if 0.0 <= u < 1.0 and 0.0 <= v < 1.0:
                dst[x, y] = src[int(u * size), int(v * size)]


def isometric_block_icon(top: Image.Image, side: Image.Image, size: int = 16) -> Image.Image:
    """The 16x16 GUI icon Minecraft shows for a block item: top, left and right faces of a cube."""
    h = size / 2.0
    q = size / 4.0
    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    _paste_face(icon, _shade(top, _CUBE_SHADE["top"]), (h, 0.5), (h - 1, q - 0.5 + 0.5), (-(h - 1), q))
    _paste_face(icon, _shade(side, _CUBE_SHADE["left"]), (1.0, q + 0.5), (h - 1, q), (0.0, size - q - 1.5))
    _paste_face(icon, _shade(side, _CUBE_SHADE["right"]), (h, size / 2.0), (h - 1, -q), (0.0, size - q - 1.5))
    return icon


def build_cube_block(texture: Image.Image, size_cm: float = 100.0) -> dict:
    h = size_cm * 0.5
    # 6 outward facing quads for standard game engine cube
    face_defs = [
        # South (-Y)
        ([(h, -h, -h), (-h, -h, -h), (-h, -h, h), (h, -h, h)], (0.0, -1.0, 0.0)),
        # North (+Y)
        ([(-h, h, -h), (h, h, -h), (h, h, h), (-h, h, h)], (0.0, 1.0, 0.0)),
        # East (+X)
        ([(h, h, -h), (h, -h, -h), (h, -h, h), (h, h, h)], (1.0, 0.0, 0.0)),
        # West (-X)
        ([(-h, -h, -h), (-h, h, -h), (-h, h, h), (-h, -h, h)], (-1.0, 0.0, 0.0)),
        # Top (+Z)
        ([(-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h)], (0.0, 0.0, 1.0)),
        # Bottom (-Z)
        ([(-h, h, -h), (h, h, -h), (h, -h, -h), (-h, -h, -h)], (0.0, 0.0, -1.0)),
    ]

    positions, normals, uvs, colors, triangles = [], [], [], [], []
    quad_uv = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]

    for verts, norm in face_defs:
        base = len(positions)
        for i, pos in enumerate(verts):
            positions.append([float(pos[0]), float(pos[1]), float(pos[2])])
            normals.append([float(norm[0]), float(norm[1]), float(norm[2])])
            uvs.append(list(quad_uv[i]))
            colors.append([1.0, 1.0, 1.0, 1.0])
        triangles.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    return {
        "positions": positions,
        "normals": normals,
        "uv": uvs,
        "colors": colors,
        "triangles": triangles,
    }
