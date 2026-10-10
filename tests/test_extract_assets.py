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


# (atlas layout tests live in the HUD atlas section at the end of this file)


def _solid(size, color):
    return Image.new("RGBA", size, color)


def _png(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


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


# --- Mob skins (zombie) from the local client.jar, never committed ---------------------------------

def _jar_with_mob(path: Path, skin: Image.Image) -> Path:
    import zipfile

    with zipfile.ZipFile(path, "w") as z:
        z.writestr("assets/minecraft/textures/entity/zombie/zombie.png", _png(skin))
        z.writestr("assets/minecraft/textures/entity/player/wide/steve.png", _png(_solid((64, 64), (9, 9, 9, 255))))
    return path


def test_export_mob_skin_writes_the_zombie_skin_unchanged(tmp_path):
    from extract_mc_assets import export_mob_skin

    skin = _solid((64, 64), (40, 120, 60, 255))
    skin.putpixel((9, 9), (1, 2, 3, 255))
    jar = _jar_with_mob(tmp_path / "client.jar", skin)
    out = export_mob_skin(jar, tmp_path / "out", "zombie")
    assert out == tmp_path / "out" / "zombie.png"
    written = Image.open(out).convert("RGBA")
    assert written.size == (64, 64)
    assert written.getpixel((9, 9)) == (1, 2, 3, 255)
    assert written.getpixel((0, 0)) == (40, 120, 60, 255)


def test_export_mob_skin_rejects_unknown_mobs_missing_entries_and_wrong_sizes(tmp_path):
    import zipfile
    from extract_mc_assets import export_mob_skin

    jar = _jar_with_mob(tmp_path / "client.jar", _solid((64, 64), (1, 1, 1, 255)))
    with pytest.raises(ValueError):
        export_mob_skin(jar, tmp_path / "out", "creeper")  # only the humanoid mobs share the player's UV layout
    empty = tmp_path / "empty.jar"
    with zipfile.ZipFile(empty, "w") as z:
        z.writestr("readme.txt", "x")
    with pytest.raises(FileNotFoundError):
        export_mob_skin(empty, tmp_path / "out", "zombie")
    legacy = _jar_with_mob(tmp_path / "legacy.jar", _solid((64, 32), (1, 2, 3, 255)))
    with pytest.raises(ValueError):
        export_mob_skin(legacy, tmp_path / "out", "zombie")


# The zombie's skin has the old layout: the left arm and leg are the right ones mirrored by the model, their blocks in the lower half are empty.
def _limb_skin():
    """A 64x64 skin whose right leg (0,16) and right arm (40,16) blocks have a different colour in every face, left blocks empty."""
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    colours = {"top": (10, 0, 0), "bottom": (20, 0, 0), "right": (30, 0, 0), "front": (40, 0, 0), "left": (50, 0, 0), "back": (60, 0, 0)}

    def paint(u0, v0):
        d, w, h = 4, 4, 12
        rects = {"top": (u0 + d, v0, w, d), "bottom": (u0 + d + w, v0, w, d), "right": (u0, v0 + d, d, h), "front": (u0 + d, v0 + d, w, h),
                 "left": (u0 + d + w, v0 + d, d, h), "back": (u0 + 2 * d + w, v0 + d, w, h)}
        for face, (x, y, fw, fh) in rects.items():
            for yy in range(fh):
                for xx in range(fw):
                    # a gradient along x so a horizontal flip is visible
                    img.putpixel((x + xx, y + yy), (colours[face][0] + xx, 100, 100, 255))

    paint(0, 16)
    paint(40, 16)
    return img


def test_mirror_empty_left_limbs_fills_them_from_the_right_ones_flipped():
    from extract_mc_assets import mirror_empty_left_limbs

    skin = _limb_skin()
    out = mirror_empty_left_limbs(skin)
    # left leg block origin (16,48): its front face (u0+4, v0+4) is the right leg's front (4,20) flipped horizontally
    assert out.getpixel((20, 52)) == skin.getpixel((4 + 3, 20))
    assert out.getpixel((23, 52)) == skin.getpixel((4 + 0, 20))
    # the outer side faces swap: the left leg's "right" face (16,52) is the right leg's "left" face (8,20) flipped
    assert out.getpixel((16, 52)) == skin.getpixel((8 + 3, 20))
    assert out.getpixel((24, 52)) == skin.getpixel((0 + 3, 20))
    # left arm block origin (32,48): front face (36,52) from the right arm's front (44,20)
    assert out.getpixel((36, 52)) == skin.getpixel((44 + 3, 20))
    # the right limbs are untouched
    assert out.getpixel((4, 20)) == skin.getpixel((4, 20))


def test_mirror_empty_left_limbs_leaves_a_skin_that_already_has_them_alone():
    from extract_mc_assets import mirror_empty_left_limbs

    skin = _limb_skin()
    skin.putpixel((20, 52), (7, 7, 7, 255))  # the left leg block is not empty
    out = mirror_empty_left_limbs(skin)
    assert out.getpixel((20, 52)) == (7, 7, 7, 255)
    assert out.getpixel((21, 52)) == (0, 0, 0, 0)  # untouched too


def test_export_mob_skin_fills_the_zombie_left_limbs(tmp_path):
    from extract_mc_assets import export_mob_skin

    jar = _jar_with_mob(tmp_path / "client.jar", _limb_skin())
    out = Image.open(export_mob_skin(jar, tmp_path / "out", "zombie")).convert("RGBA")
    assert out.getpixel((20, 52))[3] == 255  # the left leg is there now
    assert out.getpixel((36, 52))[3] == 255  # and the left arm


# --- HUD atlas: original Minecraft sprites, native sizes, vanilla layout --------------------------

HUD_KEYS = [
    "crosshair",
    "heart_container", "heart_full", "heart_half",
    "hunger_container", "hunger_full", "hunger_half",
    "hotbar", "hotbar_selection",
    "item_diamond_sword", "item_diamond_pickaxe", "item_dirt", "item_stone", "item_tnt",
    "item_golden_apple", "item_bow", "item_elytra", "item_totem_of_undying",
    "heart_absorb_full", "heart_absorb_half", "particle_crit", "particle_damage",
    *[f"particle_sweep_{i}" for i in range(8)],
    "item_arrow", "item_trident", "item_flint_and_steel", "item_ender_pearl", "item_enchanted_golden_apple",
    "item_bread", "item_cooked_beef", "item_firework_rocket", "container_top", "container_bottom",
    "block_dirt", "block_stone", "block_tnt_top", "block_tnt_side", "block_tnt_bottom",
    "heart_container_blinking", "heart_full_blinking", "heart_half_blinking", "xp_bar_background", "xp_bar_progress",
    "item_zombie_spawn_egg", "item_bow_pulling_0", "item_bow_pulling_1", "item_bow_pulling_2",
]
HUD_SIZES = {
    "heart_absorb_full": (9, 9), "heart_absorb_half": (9, 9), "particle_crit": (8, 8), "particle_damage": (8, 8),
    **{f"particle_sweep_{i}": (32, 32) for i in range(8)},
    "crosshair": (15, 15), "hotbar": (182, 22), "hotbar_selection": (24, 23),
    "heart_container": (9, 9), "heart_full": (9, 9), "heart_half": (9, 9),
    "hunger_container": (9, 9), "hunger_full": (9, 9), "hunger_half": (9, 9),
    "container_top": (176, 71), "container_bottom": (176, 96),
    "xp_bar_background": (182, 5), "xp_bar_progress": (182, 5),
    "heart_container_blinking": (9, 9), "heart_full_blinking": (9, 9), "heart_half_blinking": (9, 9),
}


def _egg_overlay() -> Image.Image:
    """A fully transparent 16x16 overlay with one opaque white pixel at (8, 8) (the spots of the egg)."""
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    img.putpixel((8, 8), (255, 255, 255, 255))
    return img


def _sprite_colors():
    return {
        "gui/sprites/hud/crosshair.png": _solid((15, 15), (10, 200, 30, 255)),
        "gui/sprites/hud/heart/container.png": _solid((9, 9), (11, 11, 11, 255)),
        "gui/sprites/hud/heart/full.png": _solid((9, 9), (200, 0, 0, 255)),
        "gui/sprites/hud/heart/half.png": _solid((9, 9), (150, 0, 0, 255)),
        "gui/sprites/hud/food_empty.png": _solid((9, 9), (22, 22, 22, 255)),
        "gui/sprites/hud/food_full.png": _solid((9, 9), (150, 90, 20, 255)),
        "gui/sprites/hud/food_half.png": _solid((9, 9), (100, 60, 10, 255)),
        "gui/sprites/hud/hotbar_selection.png": _solid((24, 23), (255, 0, 255, 255)),
        "item/diamond_sword.png": _solid((16, 16), (1, 2, 3, 255)),
        "item/diamond_pickaxe.png": _solid((16, 16), (4, 5, 6, 255)),
        "block/dirt.png": _solid((16, 16), (100, 80, 60, 255)),
        "block/stone.png": _solid((16, 16), (120, 120, 120, 255)),
        "block/tnt_side.png": _solid((16, 16), (200, 40, 30, 255)),
        "block/tnt_top.png": _solid((16, 16), (220, 220, 220, 255)),
        "item/golden_apple.png": _solid((16, 16), (16, 17, 18, 255)),
        "item/bow.png": _solid((16, 16), (19, 20, 21, 255)),
        "item/elytra.png": _solid((16, 16), (22, 23, 24, 255)),
        "item/totem_of_undying.png": _solid((16, 16), (25, 26, 27, 255)),
        "item/arrow.png": _solid((16, 16), (31, 32, 33, 255)),
        "item/trident.png": _solid((16, 16), (34, 35, 36, 255)),
        "item/flint_and_steel.png": _solid((16, 16), (37, 38, 39, 255)),
        "item/ender_pearl.png": _solid((16, 16), (40, 41, 42, 255)),
        "item/bread.png": _solid((16, 16), (43, 44, 45, 255)),
        "item/cooked_beef.png": _solid((16, 16), (46, 47, 48, 255)),
        "item/firework_rocket.png": _solid((16, 16), (49, 50, 51, 255)),
        "item/bow_pulling_0.png": _solid((16, 16), (71, 72, 73, 255)),
        "item/bow_pulling_1.png": _solid((16, 16), (74, 75, 76, 255)),
        "item/bow_pulling_2.png": _solid((16, 16), (77, 78, 79, 255)),
        "item/spawn_egg.png": _solid((16, 16), (200, 200, 200, 255)),
        "item/spawn_egg_overlay.png": _egg_overlay(),
        "block/tnt_bottom.png": _solid((16, 16), (52, 53, 54, 255)),
        "gui/sprites/hud/heart/container_blinking.png": _solid((9, 9), (61, 62, 63, 255)),
        "gui/sprites/hud/heart/full_blinking.png": _solid((9, 9), (64, 65, 66, 255)),
        "gui/sprites/hud/heart/half_blinking.png": _solid((9, 9), (67, 68, 69, 255)),
        "gui/sprites/hud/experience_bar_background.png": _solid((182, 5), (70, 71, 72, 255)),
        "gui/sprites/hud/experience_bar_progress.png": _solid((182, 5), (73, 74, 75, 255)),
    }


def _hud_jar(path: Path) -> Path:
    import zipfile

    hotbar = Image.new("RGBA", (182, 22), (0, 0, 0, 0))
    for x in range(182):
        for y in range(22):
            hotbar.putpixel((x, y), (x % 256, y * 10, 77, 255))
    files = _sprite_colors()
    files["gui/sprites/hud/hotbar.png"] = hotbar
    container = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    for x in range(256):
        for y in range(256):
            container.putpixel((x, y), (x, y, 9, 255))
    files["gui/container/generic_54.png"] = container
    with zipfile.ZipFile(path, "w") as z:
        for name, img in files.items():
            z.writestr("assets/minecraft/textures/" + name, _png(img))
    return path


def _hud_region(atlas, uv):
    u0, v0, u1, v1 = uv
    return atlas.crop((round(u0 * atlas.width), round(v0 * atlas.height), round(u1 * atlas.width), round(v1 * atlas.height)))


def test_hud_atlas_has_every_sprite_at_its_native_size_without_overlap():
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas()
    assert set(uv) == set(HUD_KEYS)
    rects = []
    for key, (u0, v0, u1, v1) in uv.items():
        x0, y0, x1, y1 = round(u0 * atlas.width), round(v0 * atlas.height), round(u1 * atlas.width), round(v1 * atlas.height)
        assert (x1 - x0, y1 - y0) == HUD_SIZES.get(key, (16, 16)), key
        assert 0 <= x0 < x1 <= atlas.width and 0 <= y0 < y1 <= atlas.height, key
        rects.append((key, x0, y0, x1, y1))
    for i, (ka, ax0, ay0, ax1, ay1) in enumerate(rects):
        for kb, bx0, by0, bx1, by1 in rects[i + 1:]:
            gap_x = max(bx0 - ax1, ax0 - bx1)
            gap_y = max(by0 - ay1, ay0 - by1)
            assert max(gap_x, gap_y) >= 1, f"{ka} and {kb} touch or overlap (point sampling would bleed)"


def test_hud_atlas_layout_is_identical_with_and_without_a_client_jar(tmp_path):
    from extract_mc_assets import build_hud_atlas

    _, fallback = build_hud_atlas()
    _, real = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    assert fallback == real  # the generated C++ UV constants are valid for both


def test_hud_atlas_copies_real_sprites_pixel_for_pixel(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    expect = {
        "crosshair": (10, 200, 30, 255),
        "heart_container": (11, 11, 11, 255), "heart_full": (200, 0, 0, 255), "heart_half": (150, 0, 0, 255),
        "hunger_container": (22, 22, 22, 255), "hunger_full": (150, 90, 20, 255), "hunger_half": (100, 60, 10, 255),
        "hotbar_selection": (255, 0, 255, 255),
        "item_diamond_sword": (1, 2, 3, 255), "item_golden_apple": (16, 17, 18, 255),
    }
    for key, color in expect.items():
        region = _hud_region(atlas, uv[key])
        pixels = {region.getpixel((x, y)) for x in range(region.width) for y in range(region.height)}
        assert pixels == {color}, f"{key}: {pixels}"


def test_hud_atlas_hotbar_is_the_whole_real_strip_unscaled(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    bar = _hud_region(atlas, uv["hotbar"])
    assert bar.size == (182, 22)
    for x, y in [(0, 0), (181, 21), (90, 10), (40, 3)]:
        assert bar.getpixel((x, y)) == (x % 256, y * 10, 77, 255)


def test_block_items_are_isometric_cubes_lit_from_above(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    dirt = _hud_region(atlas, uv["item_dirt"])
    top, left, right = dirt.getpixel((8, 4)), dirt.getpixel((4, 9)), dirt.getpixel((12, 9))
    assert top[:3] == (100, 80, 60)                      # top face keeps the full texture colour
    assert all(l < t for l, t in zip(left[:3], top[:3]))     # left side is shaded
    assert all(r < l for r, l in zip(right[:3], left[:3]))   # right side is darkest
    assert dirt.getpixel((0, 0))[3] == 0 and dirt.getpixel((15, 15))[3] == 0  # cube corners stay transparent
    assert dirt.getpixel((8, 8))[3] == 255

    tnt = _hud_region(atlas, uv["item_tnt"])
    assert tnt.getpixel((8, 4))[:3] == (220, 220, 220)       # tnt_top on top
    assert tnt.getpixel((4, 9))[0] > tnt.getpixel((4, 9))[2]  # red sides


def test_non_block_items_stay_flat(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    sword = _hud_region(atlas, uv["item_diamond_sword"])
    assert sword.getpixel((0, 0)) == (1, 2, 3, 255) and sword.getpixel((15, 15)) == (1, 2, 3, 255)


def test_container_halves_are_the_right_rows_of_generic_54(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    top = _hud_region(atlas, uv["container_top"])
    bottom = _hud_region(atlas, uv["container_bottom"])
    assert top.size == (176, 71) and bottom.size == (176, 96)
    assert top.getpixel((0, 0)) == (0, 0, 9, 255) and top.getpixel((175, 70)) == (175, 70, 9, 255)
    assert bottom.getpixel((0, 0)) == (0, 126, 9, 255) and bottom.getpixel((175, 95)) == (175, 221, 9, 255)


def test_new_item_icons_are_copied_and_the_enchanted_apple_is_a_tinted_golden_apple(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    for key, color in {"item_arrow": (31, 32, 33, 255), "item_cooked_beef": (46, 47, 48, 255), "item_firework_rocket": (49, 50, 51, 255)}.items():
        region = _hud_region(atlas, uv[key])
        assert {region.getpixel((x, y)) for x in range(16) for y in range(16)} == {color}, key
    plain = _hud_region(atlas, uv["item_golden_apple"]).getpixel((8, 8))
    glint = _hud_region(atlas, uv["item_enchanted_golden_apple"]).getpixel((8, 8))
    assert glint != plain and glint[3] == 255
    assert glint[2] > plain[2]  # shifted towards the purple glint


def test_zombie_spawn_egg_is_the_egg_tinted_with_the_zombie_colours_and_its_spots_on_top(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    egg = _hud_region(atlas, uv["item_zombie_spawn_egg"])
    # the egg is grey in the jar and tinted with the mob's primary colour 0x00AFAF (multiply): 200 * 175 / 255 = 137
    assert egg.getpixel((3, 3)) == (0, 137, 137, 255)
    # the overlay (white spots) is tinted with the secondary colour 0x799C65 and lies on top
    assert egg.getpixel((8, 8)) == (0x79, 0x9C, 0x65, 255)


def test_zombie_spawn_egg_prefers_the_mobs_own_texture_of_newer_jars(tmp_path):
    import zipfile
    from extract_mc_assets import build_hud_atlas

    jar = _hud_jar(tmp_path / "client.jar")
    with zipfile.ZipFile(jar, "a") as z:  # 1.21.4+ ships one finished texture per mob instead of a grey egg to tint
        z.writestr("assets/minecraft/textures/item/zombie_spawn_egg.png", _png(_solid((16, 16), (12, 34, 56, 255))))
    atlas, uv = build_hud_atlas(jar)
    region = _hud_region(atlas, uv["item_zombie_spawn_egg"])
    assert {region.getpixel((x, y)) for x in range(16) for y in range(16)} == {(12, 34, 56, 255)}


def test_the_three_drawn_bow_sprites_are_copied_from_the_jar_and_have_placeholders_without_one(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    for i, color in enumerate([(71, 72, 73, 255), (74, 75, 76, 255), (77, 78, 79, 255)]):
        region = _hud_region(atlas, uv[f"item_bow_pulling_{i}"])
        assert {region.getpixel((x, y)) for x in range(region.width) for y in range(region.height)} == {color}
    # without a jar the placeholder is the plain bow with a nocked arrow, visibly different from the bow at rest, and each stage differs from the one before
    plain, _ = build_hud_atlas()
    plain_uv = build_hud_atlas()[1]
    stages = [_hud_region(plain, plain_uv[f"item_bow_pulling_{i}"]).tobytes() for i in range(3)]
    rest = _hud_region(plain, plain_uv["item_bow"]).tobytes()
    assert len({rest, *stages}) == 4


def test_block_face_sprites_are_the_flat_block_textures(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    expect = {"block_dirt": (100, 80, 60, 255), "block_stone": (120, 120, 120, 255), "block_tnt_top": (220, 220, 220, 255),
              "block_tnt_side": (200, 40, 30, 255), "block_tnt_bottom": (52, 53, 54, 255)}
    for key, color in expect.items():
        region = _hud_region(atlas, uv[key])
        assert {region.getpixel((x, y)) for x in range(16) for y in range(16)} == {color}, key


def test_blinking_hearts_and_the_experience_bar_are_copied(tmp_path):
    from extract_mc_assets import build_hud_atlas

    atlas, uv = build_hud_atlas(_hud_jar(tmp_path / "client.jar"))
    expect = {"heart_container_blinking": (61, 62, 63, 255), "heart_full_blinking": (64, 65, 66, 255), "heart_half_blinking": (67, 68, 69, 255),
              "xp_bar_background": (70, 71, 72, 255), "xp_bar_progress": (73, 74, 75, 255)}
    for key, color in expect.items():
        region = _hud_region(atlas, uv[key])
        assert {region.getpixel((x, y)) for x in range(region.width) for y in range(region.height)} == {color}, key


def test_committed_generated_headers_match_the_tool_output(tmp_path):
    """The headers must be generated, never hand-edited: regenerate and compare byte for byte."""
    from extract_mc_assets import build_hud_atlas, export_hud_atlas_header, export_adapter_atlas_header

    root = Path(__file__).resolve().parent.parent
    atlas, uv = build_hud_atlas()
    core = tmp_path / "hud_atlas.hpp"
    export_hud_atlas_header(atlas, uv, core)
    assert core.read_text(encoding="utf-8") == (root / "include/mc/hud_atlas.hpp").read_text(encoding="utf-8")

    adapter = tmp_path / "sekiro_hud_atlas.hpp"
    export_adapter_atlas_header(uv, adapter, "sekiro::hud")
    assert adapter.read_text(encoding="utf-8") == (root / "adapters/sekiro/include/sekiro_hud_atlas.hpp").read_text(encoding="utf-8")

    committed_png = Image.open(root / "assets/source/textures/mc_hud_atlas.png").convert("RGBA")
    assert committed_png.tobytes() == atlas.tobytes()


def test_parse_geometry_json_bedrock_1_12():
    from extract_mc_assets import parse_geometry_json

    sample_json = """{
        "format_version": "1.12.0",
        "minecraft:geometry": [
            {
                "description": {
                    "identifier": "geometry.creeper",
                    "texture_width": 64,
                    "texture_height": 32
                },
                "bones": [
                    {
                        "name": "head",
                        "pivot": [0, 18, 0],
                        "cubes": [
                            {"origin": [-4, 18, -4], "size": [8, 8, 8], "uv": [0, 0]}
                        ]
                    },
                    {
                        "name": "body",
                        "pivot": [0, 18, 0],
                        "cubes": [
                            {"origin": [-4, 6, -2], "size": [8, 12, 4], "uv": [16, 16], "inflate": 0.25}
                        ]
                    },
                    {
                        "name": "leg0",
                        "parent": "body",
                        "pivot": [-2, 6, 4],
                        "rotation": [0, 15, 0],
                        "cubes": [
                            {"origin": [-4, 0, 2], "size": [4, 6, 4], "uv": [0, 16], "mirror": true}
                        ]
                    }
                ]
            }
        ]
    }"""
    geo = parse_geometry_json(sample_json)
    assert geo["identifier"] == "geometry.creeper"
    assert geo["texture_width"] == 64
    assert geo["texture_height"] == 32
    assert len(geo["bones"]) == 3

    head = geo["bones"][0]
    assert head["name"] == "head"
    assert head["pivot"] == (0, 18, 0)
    assert len(head["cubes"]) == 1
    assert head["cubes"][0]["origin"] == (-4, 18, -4)
    assert head["cubes"][0]["size"] == (8, 8, 8)
    assert head["cubes"][0]["uv"] == (0, 0)

    body = geo["bones"][1]
    assert body["name"] == "body"
    assert body["cubes"][0]["inflate"] == 0.25

    leg0 = geo["bones"][2]
    assert leg0["name"] == "leg0"
    assert leg0["parent"] == "body"
    assert leg0["rotation"] == (0, 15, 0)
    assert leg0["cubes"][0]["mirror"] is True


def test_parse_geometry_json_bedrock_1_8():
    from extract_mc_assets import parse_geometry_json

    sample_1_8 = """{
        "format_version": "1.8.0",
        "geometry.zombie": {
            "texturewidth": 64,
            "textureheight": 64,
            "bones": [
                {
                    "name": "head",
                    "pivot": [0, 24, 0],
                    "cubes": [
                        {"origin": [-4, 24, -4], "size": [8, 8, 8], "uv": [0, 0]}
                    ]
                }
            ]
        }
    }"""
    geo = parse_geometry_json(sample_1_8)
    assert geo["identifier"] == "geometry.zombie"
    assert geo["texture_width"] == 64
    assert geo["texture_height"] == 64
    assert len(geo["bones"]) == 1
    assert geo["bones"][0]["name"] == "head"


def test_build_geometry_mesh_generates_valid_triangles():
    from extract_mc_assets import parse_geometry_json, build_geometry_mesh

    sample_json = """{
        "format_version": "1.12.0",
        "minecraft:geometry": [
            {
                "description": {
                    "identifier": "geometry.box",
                    "texture_width": 64,
                    "texture_height": 32
                },
                "bones": [
                    {
                        "name": "cube",
                        "pivot": [0, 0, 0],
                        "cubes": [
                            {"origin": [-2, 0, -2], "size": [4, 4, 4], "uv": [0, 0]}
                        ]
                    }
                ]
            }
        ]
    }"""
    geo = parse_geometry_json(sample_json)
    parts = build_geometry_mesh(geo)
    assert "cube" in parts
    mesh = parts["cube"]
    # 6 faces * 4 vertices = 24 vertices
    assert len(mesh["positions"]) == 24
    assert len(mesh["normals"]) == 24
    assert len(mesh["uv"]) == 24
    # 6 faces * 2 triangles * 3 indices = 36 indices
    assert len(mesh["triangles"]) == 36
    for u, v in mesh["uv"]:
        assert 0.0 <= u <= 1.0
        assert 0.0 <= v <= 1.0


def test_zombie_geo_json_file_is_valid():
    from extract_mc_assets import parse_geometry_json, build_geometry_mesh

    root = Path(__file__).resolve().parent.parent
    zombie_path = root / "assets/source/models/entities/zombie.geo.json"
    assert zombie_path.exists(), f"Missing {zombie_path}"

    content = zombie_path.read_text(encoding="utf-8")
    geo = parse_geometry_json(content)
    assert geo["identifier"] == "geometry.zombie"
    assert geo["texture_width"] == 64
    assert geo["texture_height"] == 64
    assert len(geo["bones"]) == 12

    bone_names = {b["name"] for b in geo["bones"]}
    assert "head" in bone_names
    assert "body" in bone_names
    assert "right_arm" in bone_names
    assert "left_arm" in bone_names
    assert "right_leg" in bone_names
    assert "left_leg" in bone_names

    parts = build_geometry_mesh(geo)
    assert len(parts) == 12
    for name, mesh in parts.items():
        assert len(mesh["positions"]) >= 24
        assert len(mesh["triangles"]) >= 36


def test_creeper_and_skeleton_geo_json_files_are_valid():
    from extract_mc_assets import parse_geometry_json, build_geometry_mesh

    root = Path(__file__).resolve().parent.parent
    for mob_id, filename in [("geometry.creeper", "creeper.geo.json"), ("geometry.skeleton", "skeleton.geo.json")]:
        p = root / "assets/source/models/entities" / filename
        assert p.exists(), f"Missing {p}"
        geo = parse_geometry_json(p.read_text(encoding="utf-8"))
        assert geo["identifier"] == mob_id
        parts = build_geometry_mesh(geo)
        assert len(parts) >= 6


def test_export_entity_models(tmp_path: Path):
    from extract_mc_assets import export_entity_models

    root = Path(__file__).resolve().parent.parent
    models_dir = root / "assets/source/models"
    out_dir = tmp_path / "mods/mc_adapter"

    copied = export_entity_models(models_dir, out_dir)
    assert len(copied) >= 3
    copied_names = {f.name for f in copied}
    assert "zombie.geo.json" in copied_names
    assert "creeper.geo.json" in copied_names
    assert "skeleton.geo.json" in copied_names
    for f in copied:
        assert f.exists()
        assert f.stat().st_size > 0


