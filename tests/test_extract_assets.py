from __future__ import annotations
import io
from pathlib import Path
import pytest
from PIL import Image

import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from extract_mc_assets import (
    PARTS,
    cuboid,
    mesh_from_faces,
    export_obj,
    build_steve_parts,
    build_cube_block
)


def test_cuboid_generates_six_faces():
    faces = cuboid(origin=(0, 0, 0), size=(8, 8, 8), uv=(0, 0), inflate=0)
    assert len(faces) == 6
    for verts, uv_rect in faces:
        assert len(verts) == 4
        assert len(uv_rect) == 4


def test_mesh_from_faces_with_synthetic_texture():
    # 64x64 RGBA dummy texture
    img = Image.new("RGBA", (64, 64), (255, 0, 0, 255))
    faces = cuboid(origin=(0, 0, 0), size=(4, 4, 4), uv=(0, 0))
    mesh = mesh_from_faces(faces, img)

    assert len(mesh["positions"]) > 0
    assert len(mesh["normals"]) == len(mesh["positions"])
    assert len(mesh["uv"]) == len(mesh["positions"])
    assert len(mesh["triangles"]) % 3 == 0

    # Ensure all UVs are in [0, 1] range
    for u, v in mesh["uv"]:
        assert 0.0 <= u <= 1.0
        assert 0.0 <= v <= 1.0


def test_export_obj_format():
    img = Image.new("RGBA", (64, 64), (0, 255, 0, 255))
    faces = cuboid(origin=(-2, -2, -2), size=(4, 4, 4), uv=(0, 0))
    mesh = mesh_from_faces(faces, img)
    obj_str = export_obj("test_cube", mesh)

    lines = obj_str.strip().split("\n")
    v_lines = [l for l in lines if l.startswith("v ")]
    vt_lines = [l for l in lines if l.startswith("vt ")]
    vn_lines = [l for l in lines if l.startswith("vn ")]
    f_lines = [l for l in lines if l.startswith("f ")]

    assert len(v_lines) == len(mesh["positions"])
    assert len(vt_lines) == len(mesh["uv"])
    assert len(vn_lines) == len(mesh["normals"])
    assert len(f_lines) == len(mesh["triangles"]) // 3


def test_build_steve_parts_twelve_meshes():
    skin_img = Image.new("RGBA", (64, 64), (128, 128, 128, 255))
    parts = build_steve_parts(skin_img)

    assert len(parts) == 12
    part_names = [p["name"] for p in parts]
    expected_names = [name for name, *_ in PARTS]
    assert part_names == expected_names

    for part in parts:
        assert len(part["mesh"]["positions"]) > 0
        assert len(part["mesh"]["triangles"]) > 0


def test_build_cube_block_dimensions():
    block_img = Image.new("RGBA", (16, 16), (100, 70, 40, 255))
    cube_mesh = build_cube_block(block_img, size_cm=100.0)

    # Coordinate extents should match [-50, 50] cm
    xs = [p[0] for p in cube_mesh["positions"]]
    ys = [p[1] for p in cube_mesh["positions"]]
    zs = [p[2] for p in cube_mesh["positions"]]

    assert pytest.approx(min(xs), abs=0.1) == -50.0
    assert pytest.approx(max(xs), abs=0.1) == 50.0
    assert pytest.approx(min(ys), abs=0.1) == -50.0
    assert pytest.approx(max(ys), abs=0.1) == 50.0
    assert pytest.approx(min(zs), abs=0.1) == -50.0
    assert pytest.approx(max(zs), abs=0.1) == 50.0


def test_build_hud_atlas_contains_expected_uvs():
    from extract_mc_assets import build_hud_atlas

    atlas_img, uv_map = build_hud_atlas()
    assert isinstance(atlas_img, Image.Image)
    assert atlas_img.width >= 128
    assert atlas_img.height >= 128
    assert atlas_img.mode == "RGBA"

    required_keys = [
        "crosshair",
        "heart_full",
        "hunger_full",
        "hotbar_slot",
        "hotbar_cursor",
        "item_diamond_sword",
        "item_diamond_pickaxe",
        "item_dirt",
        "item_stone",
        "item_tnt",
        "item_golden_apple",
        "item_bow",
        "item_elytra",
        "item_totem_of_undying",
    ]

    for key in required_keys:
        assert key in uv_map, f"Missing UV mapping for {key}"
        u0, v0, u1, v1 = uv_map[key]
        assert 0.0 <= u0 < u1 <= 1.0, f"Invalid U range for {key}: {u0}..{u1}"
        assert 0.0 <= v0 < v1 <= 1.0, f"Invalid V range for {key}: {v0}..{v1}"



# --- real client.jar sprites must be used (not the procedural fallback) ---------------------------

def _solid(size, color):
    return Image.new("RGBA", size, color)


def _png(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def _fake_client_jar(path: Path) -> Path:
    import zipfile

    base = "assets/minecraft/textures/"
    hotbar = Image.new("RGBA", (182, 22), (0, 0, 0, 0))
    for x in range(182):  # recognisable vertical gradient so a crop can be located exactly
        for y in range(22):
            hotbar.putpixel((x, y), (x % 256, y * 10, 77, 255))
    selection = _solid((24, 23), (255, 0, 255, 255))
    crosshair = _solid((15, 15), (10, 200, 30, 255))
    files = {
        "gui/sprites/hud/crosshair.png": crosshair,
        "gui/sprites/hud/heart/full.png": _solid((9, 9), (200, 0, 0, 255)),
        "gui/sprites/hud/food_full.png": _solid((9, 9), (150, 90, 20, 255)),
        "gui/sprites/hud/hotbar.png": hotbar,
        "gui/sprites/hud/hotbar_selection.png": selection,
        "item/diamond_sword.png": _solid((16, 16), (1, 2, 3, 255)),
        "item/diamond_pickaxe.png": _solid((16, 16), (4, 5, 6, 255)),
        "block/dirt.png": _solid((16, 16), (7, 8, 9, 255)),
        "block/stone.png": _solid((16, 16), (10, 11, 12, 255)),
        "block/tnt_side.png": _solid((16, 16), (13, 14, 15, 255)),
        "item/golden_apple.png": _solid((16, 16), (16, 17, 18, 255)),
        "item/bow.png": _solid((16, 16), (19, 20, 21, 255)),
        "item/elytra.png": _solid((16, 16), (22, 23, 24, 255)),
        "item/totem_of_undying.png": _solid((16, 16), (25, 26, 27, 255)),
    }
    with zipfile.ZipFile(path, "w") as z:
        for name, img in files.items():
            z.writestr(base + name, _png(img))
    return path


def _region(atlas: Image.Image, uv, w=None, h=None):
    u0, v0, u1, v1 = uv
    return atlas.crop((round(u0 * atlas.width), round(v0 * atlas.height), round(u1 * atlas.width), round(v1 * atlas.height)))


def test_hud_atlas_uses_real_sprites_when_client_jar_given(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_fake_client_jar(tmp_path / "client.jar"))

    assert _region(atlas, uv["item_dirt"]).getpixel((8, 8)) == (7, 8, 9, 255)
    assert _region(atlas, uv["item_diamond_sword"]).getpixel((8, 8)) == (1, 2, 3, 255)
    assert _region(atlas, uv["heart_full"]).getpixel((4, 4)) == (200, 0, 0, 255)
    assert _region(atlas, uv["hunger_full"]).getpixel((4, 4)) == (150, 90, 20, 255)


def test_hud_atlas_keeps_layout_identical_with_and_without_jar(tmp_path):
    from extract_mc_assets import build_hud_atlas

    _, uv_fallback = build_hud_atlas()
    _, uv_real = build_hud_atlas(_fake_client_jar(tmp_path / "client.jar"))
    assert uv_fallback == uv_real  # C++ UV constants must stay valid for either atlas


def test_hud_atlas_hotbar_slot_comes_from_real_hotbar_strip(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_fake_client_jar(tmp_path / "client.jar"))
    slot = _region(atlas, uv["hotbar_slot"])
    assert slot.size == (24, 24)
    # one 22x22 cell of the strip (cell index 1 => x = 20..42) centred on the 24x24 canvas
    assert slot.getpixel((1, 1)) == (20, 0, 77, 255)
    assert slot.getpixel((22, 21)) == (41, 200, 77, 255)  # canvas (22,21) = cell (21,20) = strip (41,20)
    assert slot.getpixel((0, 0))[3] == 0 and slot.getpixel((23, 23))[3] == 0  # transparent margin

    cursor = _region(atlas, uv["hotbar_cursor"])
    assert cursor.getpixel((12, 10)) == (255, 0, 255, 255)


def test_hud_atlas_crosshair_is_centred_not_stretched(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_fake_client_jar(tmp_path / "client.jar"))
    ch = _region(atlas, uv["crosshair"])
    assert ch.size == (16, 16)
    colored = [(x, y) for x in range(16) for y in range(16) if ch.getpixel((x, y))[3] > 0]
    assert len(colored) == 15 * 15  # 15x15 source pasted 1:1, no resampling holes or doubled rows


# --- Steve skin export (real skin from the local client.jar, never committed) ---------------------

def _jar_with_skin(path: Path, skin: Image.Image, slim: bool = False) -> Path:
    import zipfile

    with zipfile.ZipFile(path, "w") as z:
        z.writestr("assets/minecraft/textures/entity/player/wide/steve.png", _png(skin))
        if slim:
            z.writestr("assets/minecraft/textures/entity/player/slim/steve.png", _png(_solid((64, 64), (9, 9, 9, 255))))
    return path


def test_export_steve_skin_writes_the_wide_skin_unchanged(tmp_path):
    from extract_mc_assets import export_steve_skin

    skin = _solid((64, 64), (12, 34, 56, 255))
    skin.putpixel((8, 8), (200, 100, 50, 255))
    jar = _jar_with_skin(tmp_path / "client.jar", skin, slim=True)

    out = export_steve_skin(jar, tmp_path / "out")
    assert out == tmp_path / "out" / "steve.png"
    written = Image.open(out).convert("RGBA")
    assert written.size == (64, 64)
    assert written.getpixel((8, 8)) == (200, 100, 50, 255)
    assert written.getpixel((0, 0)) == (12, 34, 56, 255)  # the wide model, not the slim one


def test_export_steve_skin_rejects_missing_or_wrong_size(tmp_path):
    import zipfile
    from extract_mc_assets import export_steve_skin

    empty = tmp_path / "empty.jar"
    with zipfile.ZipFile(empty, "w") as z:
        z.writestr("readme.txt", "x")
    with pytest.raises(FileNotFoundError):
        export_steve_skin(empty, tmp_path / "out")

    legacy = _jar_with_skin(tmp_path / "legacy.jar", _solid((64, 32), (1, 2, 3, 255)))
    with pytest.raises(ValueError):
        export_steve_skin(legacy, tmp_path / "out")  # the rig's UV table assumes the 64x64 layout
