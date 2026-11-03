"""Bake an extracted Pokemon DAE into an MCSM static mesh + one packed atlas.

The DAE exports from the Switch model viewer carry a full skin (92 joints) but no animation:
every sampled frame is identical. mc-core's world-mob path draws one rigid mesh per model, so
this tool flattens the bind-pose geometry into one vertex/index buffer and packs the albedo
textures into a single PNG. The runtime type is mc::model::StaticMesh.

    uv run --with pillow python tools/pokemon/build_static_mesh.py \
        --dae "D:/.../pm0491_00_00.dae" --name darkrai --out-dir build/pokemon

Outputs <name>.mcsm, <name>.png and <name>.meta.json. Extracted assets are never committed;
the output directory is expected to be gitignored (build/).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import unquote
import xml.etree.ElementTree as ET

from PIL import Image

MCSM_MAGIC = 0x4D53434D  # "MCSM"
MCSM_VERSION = 1
MAX_VERTICES = 65535
ATLAS_PAD = 4
MAX_ATLAS = 4096


# ---------------------------------------------------------------------------------------------
# COLLADA helpers
# ---------------------------------------------------------------------------------------------


def _tag(el: ET.Element) -> str:
    return el.tag.rsplit("}", 1)[-1]


def _kids(el: ET.Element, name: str) -> list[ET.Element]:
    return [c for c in el if _tag(c) == name]


def _kid(el: ET.Element, name: str) -> ET.Element | None:
    for c in el:
        if _tag(c) == name:
            return c
    return None


@dataclass
class Source:
    values: list[float]
    count: int
    stride: int
    offset: int

    def vec(self, index: int, n: int) -> tuple[float, ...]:
        base = self.offset + index * self.stride
        if base + n > len(self.values):
            raise IndexError(f"source read out of range: index {index} needs {n} floats")
        return tuple(self.values[base : base + n])


@dataclass
class Material:
    name: str
    image: Path | None


@dataclass
class Triangle:
    material: str
    corners: list[tuple[tuple[float, float, float], tuple[float, float]]]


def _read_sources(root: ET.Element) -> dict[str, Source]:
    arrays: dict[str, list[float]] = {}
    for el in root.iter():
        if _tag(el) != "float_array":
            continue
        if el.get("id"):
            arrays[el.get("id")] = [float(v) for v in (el.text or "").split()]

    sources: dict[str, Source] = {}
    for el in root.iter():
        if _tag(el) != "source" or not el.get("id"):
            continue
        fa = _kid(el, "float_array")
        values = arrays.get(fa.get("id"), []) if fa is not None else []
        tech = _kid(el, "technique_common")
        accessor = _kid(tech, "accessor") if tech is not None else None
        stride = int(accessor.get("stride", "1")) if accessor is not None else 1
        count = int(accessor.get("count", "0")) if accessor is not None else len(values) // max(stride, 1)
        offset = int(accessor.get("offset", "0")) if accessor is not None else 0
        if accessor is not None and accessor.get("source", "").startswith("#"):
            ref = arrays.get(accessor.get("source")[1:])
            if ref is not None:
                values = ref
        sources[el.get("id")] = Source(values, count, stride, offset)
    return sources


def _read_images(root: ET.Element) -> dict[str, str]:
    images: dict[str, str] = {}
    for el in root.iter():
        if _tag(el) != "image" or not el.get("id"):
            continue
        init = _kid(el, "init_from")
        if init is not None and init.text:
            images[el.get("id")] = init.text.strip()
    return images


def _read_effects(root: ET.Element) -> dict[str, str]:
    """effect id -> image id (through diffuse texture -> sampler -> surface)."""
    effects: dict[str, str] = {}
    for el in root.iter():
        if _tag(el) != "effect" or not el.get("id"):
            continue
        # sid -> source id for sampler2D/surface newparams
        surfaces: dict[str, str] = {}
        samplers: dict[str, str] = {}
        for newparam in el.iter():
            if _tag(newparam) != "newparam":
                continue
            sid = newparam.get("sid")
            surface = _kid(newparam, "surface")
            sampler = _kid(newparam, "sampler2D")
            if surface is not None and sid:
                init = _kid(surface, "init_from")
                if init is not None and init.text:
                    surfaces[sid] = init.text.strip()
            elif sampler is not None and sid:
                source = _kid(sampler, "source")
                if source is not None and source.text:
                    samplers[sid] = source.text.strip()
        # The diffuse texture names a sampler; follow it to the surface, then to the image.
        image = None
        for diffuse in el.iter():
            if _tag(diffuse) != "diffuse":
                continue
            texture = _kid(diffuse, "texture")
            if texture is not None and texture.get("texture"):
                sampler = samplers.get(texture.get("texture"))
                if sampler:
                    image = surfaces.get(sampler)
        if image:
            effects[el.get("id")] = image
    return effects


def _read_materials(root: ET.Element, images: dict[str, str], effects: dict[str, str]) -> dict[str, Material]:
    materials: dict[str, Material] = {}
    for el in root.iter():
        if _tag(el) != "material" or not el.get("id"):
            continue
        name = el.get("name") or el.get("id")
        instance = _kid(el, "instance_effect")
        effect_id = (instance.get("url") or "").lstrip("#") if instance is not None else ""
        image_id = effects.get(effect_id)
        image_ref = images.get(image_id) if image_id else None
        materials[el.get("id")] = Material(name=name, image=None if image_ref is None else Path(image_ref))
    return materials


def _read_symbols(root: ET.Element) -> dict[str, str]:
    symbols: dict[str, str] = {}
    for el in root.iter():
        if _tag(el) != "instance_material":
            continue
        symbol = el.get("symbol")
        target = (el.get("target") or "").lstrip("#")
        if symbol and target:
            symbols[symbol] = target
    return symbols


def _read_triangles(root: ET.Element, sources: dict[str, Source]) -> list[Triangle]:
    triangles: list[Triangle] = []
    for geometry in root.iter():
        if _tag(geometry) != "geometry":
            continue
        mesh = _kid(geometry, "mesh")
        if mesh is None:
            continue
        vertices = _kid(mesh, "vertices")
        vmap: dict[str, str] = {}
        if vertices is not None:
            for inp in _kids(vertices, "input"):
                if inp.get("semantic") and inp.get("source"):
                    vmap[inp.get("semantic")] = inp.get("source").lstrip("#")

        for prim in _kids(mesh, "triangles"):
            inputs = _kids(prim, "input")
            offsets = {inp.get("semantic"): int(inp.get("offset", "0")) for inp in inputs}
            srcs = {inp.get("semantic"): (inp.get("source") or "").lstrip("#") for inp in inputs}
            stride = max(offsets.values()) + 1 if offsets else 1
            p_el = _kid(prim, "p")
            if p_el is None or p_el.text is None:
                continue
            indices = [int(v) for v in p_el.text.split()]
            count = int(prim.get("count", "0"))
            material = prim.get("material", "")

            pos_src = srcs.get("POSITION") or vmap.get("POSITION")
            uv_src = srcs.get("TEXCOORD") or vmap.get("TEXCOORD")
            if pos_src is None or pos_src not in sources:
                continue
            pos_off = offsets.get("POSITION", offsets.get("VERTEX", 0))
            uv_off = offsets.get("TEXCOORD", offsets.get("VERTEX", 0))
            vertex_off = offsets.get("VERTEX", 0)

            for t in range(count):
                corners = []
                for c in range(3):
                    base = (t * 3 + c) * stride
                    vertex_index = indices[base + vertex_off]
                    pos_index = indices[base + pos_off] if "POSITION" in offsets else vertex_index
                    uv_index = indices[base + uv_off] if "TEXCOORD" in offsets else vertex_index
                    pos = sources[pos_src].vec(pos_index, 3)
                    uv = sources[uv_src].vec(uv_index, 2) if uv_src in sources else (0.0, 0.0)
                    corners.append(((pos[0], pos[1], pos[2]), (uv[0], uv[1])))
                triangles.append(Triangle(material=material, corners=corners))
    return triangles


# ---------------------------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------------------------


def _norm_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def _resolve_image(ref: Path, dae_dir: Path) -> Path | None:
    """Map a COLLADA init_from (spaces, %20, or a sanitized __Composite__ name) to a real file."""
    raw = unquote(str(ref)).replace("\\", "/").split("/")[-1]
    direct = dae_dir / raw
    if direct.is_file():
        return direct
    wanted = _norm_name(raw)
    candidates = {_norm_name(p.name): p for p in dae_dir.glob("*.png")}
    if wanted in candidates:
        return candidates[wanted]
    # Sanitized names replace ".", " ", "(", ")" with "_"; fall back to a token match.
    for key, path in candidates.items():
        if key == wanted.replace("_", ""):
            return path
    return None


def _albedo_fallback(image: Path, dae_dir: Path) -> Path:
    """If the bound image is an auxiliary map, find the matching albedo sibling."""
    stem = image.stem
    base = re.split(r"_(?:lym|nrm|ao|rgn|msk|mtl)", stem)[0]
    if base == stem:
        return image
    for suffix in ("_alb_color", "_alb (Composite)", "_alb__Composite__", "_alb"):
        candidate = dae_dir / f"{base}{suffix}.png"
        if candidate.is_file():
            return candidate
    return image


def _pad_edge(img: Image.Image, pad: int) -> Image.Image:
    w, h = img.size
    out = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    out.paste(img, (pad, pad))
    if pad > 0:
        out.paste(img.crop((0, 0, 1, h)).resize((pad, h), Image.NEAREST), (0, pad))
        out.paste(img.crop((w - 1, 0, w, h)).resize((pad, h), Image.NEAREST), (w + pad, pad))
        out.paste(img.crop((0, 0, w, 1)).resize((w, pad), Image.NEAREST), (pad, 0))
        out.paste(img.crop((0, h - 1, w, h)).resize((w, pad), Image.NEAREST), (pad, h + pad))
        corners = [
            ((0, 0), img.getpixel((0, 0))),
            ((w + pad, 0), img.getpixel((w - 1, 0))),
            ((0, h + pad), img.getpixel((0, h - 1))),
            ((w + pad, h + pad), img.getpixel((w - 1, h - 1))),
        ]
        for (x, y), colour in corners:
            out.paste(Image.new("RGBA", (pad, pad), colour), (x, y))
    return out


def _next_pow2(value: int) -> int:
    return 1 << max(0, (value - 1).bit_length())


def _pack_atlas(images: dict[Path, Image.Image], width: int) -> tuple[Image.Image, dict[Path, tuple[int, int, int, int]]]:
    """Shelf-pack the images, returning the atlas and each image's inner rect (x, y, w, h)."""
    order = sorted(images.items(), key=lambda kv: kv[1].height, reverse=True)
    x = y = row_h = 0
    rects: dict[Path, tuple[int, int, int, int]] = {}
    for path, img in order:
        w, h = img.size
        if x + w + 2 * ATLAS_PAD > width and x > 0:
            x = 0
            y += row_h + 2 * ATLAS_PAD
            row_h = 0
        rects[path] = (x + ATLAS_PAD, y + ATLAS_PAD, w, h)
        x += w + 2 * ATLAS_PAD
        row_h = max(row_h, h)
    height = _next_pow2(max(1024, y + row_h + 2 * ATLAS_PAD))
    atlas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    for path, img in order:
        x0, y0, _, _ = rects[path]
        atlas.paste(_pad_edge(img, ATLAS_PAD), (x0 - ATLAS_PAD, y0 - ATLAS_PAD))
    return atlas, rects


# ---------------------------------------------------------------------------------------------
# Bake
# ---------------------------------------------------------------------------------------------


def _material_image(material: Material, dae_dir: Path) -> Path:
    image = material.image
    if image is None:
        raise SystemExit(f"material '{material.name}' has no diffuse texture in the DAE")
    resolved = _resolve_image(image, dae_dir)
    if resolved is None:
        raise SystemExit(f"cannot resolve texture '{image}' next to {dae_dir}")
    return _albedo_fallback(resolved, dae_dir)


def bake(dae: Path, name: str, out_dir: Path, scale_override: float, yaw_offset_deg: float,
         atlas_width: int, flip_v: bool, center_xz: bool, up_axis: str) -> dict:
    root = ET.parse(dae).getroot()
    sources = _read_sources(root)
    images = _read_images(root)
    effects = _read_effects(root)
    materials = _read_materials(root, images, effects)
    symbols = _read_symbols(root)
    triangles = _read_triangles(root, sources)
    if not triangles:
        raise SystemExit(f"no triangles found in {dae}")

    # material attribute -> material id -> diffuse image
    material_ids: dict[str, str] = {}
    for tri in triangles:
        key = tri.material
        material_ids[key] = symbols.get(key, key)
    used: dict[str, Path] = {}
    for key, material_id in material_ids.items():
        material = materials.get(material_id)
        if material is None:
            raise SystemExit(f"triangle material '{key}' -> '{material_id}' is not defined")
        used[key] = _material_image(material, dae.parent)

    # Atlas: one tile per unique image.
    loaded: dict[Path, Image.Image] = {}
    for path in used.values():
        if path not in loaded:
            loaded[path] = Image.open(path).convert("RGBA")
    atlas, rects = _pack_atlas(loaded, atlas_width)
    if atlas.width > MAX_ATLAS or atlas.height > MAX_ATLAS:
        raise SystemExit(f"atlas {atlas.width}x{atlas.height} exceeds {MAX_ATLAS}; pass a smaller --atlas")

    # Positions: axis, units, feet and horizontal centre.
    positions = [p for tri in triangles for p, _ in tri.corners]
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    bbox_min = (min(xs), min(ys), min(zs))
    bbox_max = (max(xs), max(ys), max(zs))
    largest = max(bbox_max[i] - bbox_min[i] for i in range(3))
    unit_scale = 0.01 if largest > 20.0 else 1.0
    scale = unit_scale * scale_override
    cx = (bbox_min[0] + bbox_max[0]) * 0.5 if center_xz else 0.0
    cz = (bbox_min[2] + bbox_max[2]) * 0.5 if center_xz else 0.0

    def transform(pos: tuple[float, float, float]) -> tuple[float, float, float]:
        x, y, z = pos
        if up_axis == "z":
            x, y, z = x, z, -y
        return ((x - cx) * scale, (y - bbox_min[1]) * scale, (z - cz) * scale)

    def remap_uv(uv: tuple[float, float], path: Path) -> tuple[float, float]:
        u, v = uv
        # The exporter shifts some materials' V by ±1 (body_a at -1..0, body_c at 1..2) while
        # others are in 0..1. Wrap only the out-of-range values so exact 0/1 edges stay put.
        if u < 0.0 or u > 1.0:
            u = u % 1.0
        if v < 0.0 or v > 1.0:
            v = v % 1.0
        if flip_v:
            v = 1.0 - v
        x0, y0, w, h = rects[path]
        # texel centres keep point sampling inside the tile
        return ((x0 + 0.5 + u * (w - 1)) / atlas.width, (y0 + 0.5 + v * (h - 1)) / atlas.height)

    vertices: list[tuple[float, float, float, float, float]] = []
    indices: list[int] = []
    dedup: dict[tuple, int] = {}
    for tri in triangles:
        path = used[tri.material]
        for pos, uv in tri.corners:
            p = transform(pos)
            t = remap_uv(uv, path)
            key = tuple(round(v, 6) for v in (*p, *t))
            index = dedup.get(key)
            if index is None:
                index = len(vertices)
                if index >= MAX_VERTICES:
                    raise SystemExit(f"{len(vertices) + 1} vertices exceed the 16-bit index limit")
                dedup[key] = index
                vertices.append((p[0], p[1], p[2], t[0], t[1]))
            indices.append(index)

    out_dir.mkdir(parents=True, exist_ok=True)
    atlas_path = out_dir / f"{name}.png"
    atlas.save(atlas_path)

    mesh_path = out_dir / f"{name}.mcsm"
    with mesh_path.open("wb") as f:
        f.write(struct.pack("<III", MCSM_MAGIC, MCSM_VERSION, len(vertices)))
        f.write(struct.pack("<II", len(indices), 1))
        encoded = name.encode("utf-8")
        f.write(struct.pack("<I", len(encoded)))
        f.write(encoded)
        f.write(struct.pack("<f", math.radians(yaw_offset_deg)))
        tex = atlas_path.name.encode("utf-8")
        mat = b"atlas"
        f.write(struct.pack("<I", len(mat)))
        f.write(mat)
        f.write(struct.pack("<I", len(tex)))
        f.write(tex)
        f.write(struct.pack("<II", 0, len(indices)))
        for v in vertices:
            f.write(struct.pack("<fffff", *v))
        for i in indices:
            f.write(struct.pack("<H", i))

    meta = {
        "name": name,
        "source": str(dae),
        "scale": scale,
        "unit_scale": unit_scale,
        "yaw_offset_deg": yaw_offset_deg,
        "vertices": len(vertices),
        "triangles": len(indices) // 3,
        "atlas": {"width": atlas.width, "height": atlas.height},
        "bbox": {"min": list(bbox_min), "max": list(bbox_max)},
        "materials": {
            key: {
                "material": material_ids[key],
                "image": str(used[key].name),
                "rect": list(rects[used[key]]),
            }
            for key in sorted(used)
        },
    }
    (out_dir / f"{name}.meta.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False), encoding="utf-8")
    return meta


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dae", required=True, type=Path, help="extracted .dae file")
    parser.add_argument("--name", required=True, help="model name, e.g. darkrai (also the output file stem)")
    parser.add_argument("--out-dir", type=Path, default=Path("build/pokemon"), help="output directory")
    parser.add_argument("--scale", type=float, default=1.0, help="extra uniform scale on top of the unit detection")
    parser.add_argument("--yaw-offset", type=float, default=0.0, help="degrees added to the mob heading")
    parser.add_argument("--atlas", type=int, default=1024, help="atlas width in pixels")
    parser.add_argument("--up-axis", choices=("y", "z"), default="y", help="source up axis")
    parser.add_argument("--no-flip-v", action="store_true", help="keep COLLADA's bottom-left V origin")
    parser.add_argument("--no-center-xz", action="store_true", help="keep the source X/Z origin")
    args = parser.parse_args(argv)

    meta = bake(args.dae, args.name, args.out_dir, args.scale, args.yaw_offset, args.atlas,
                not args.no_flip_v, not args.no_center_xz, args.up_axis)
    print(json.dumps(meta, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
