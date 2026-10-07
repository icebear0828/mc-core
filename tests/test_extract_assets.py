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

