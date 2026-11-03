"""The DAE -> MCSM converter, exercised on synthetic fixtures (no extracted assets in the repo)."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent.parent / "tools" / "pokemon"))

from build_static_mesh import MCSM_MAGIC, MCSM_VERSION, bake  # noqa: E402

COLLADA_HEAD = '<?xml version="1.0" encoding="utf-8"?>\n<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">'

COMMON_LIBRARIES = """
  <library_images><image id="Image"><init_from>{image}</init_from></image></library_images>
  <library_effects>
    <effect id="Effect"><profile_COMMON>
      <newparam sid="Image-surface"><surface type="2D"><init_from>Image</init_from></surface></newparam>
      <newparam sid="Image-sampler"><sampler2D><source>Image-surface</source></sampler2D></newparam>
      <technique sid="common"><phong><diffuse><texture texture="Image-sampler" texcoord=""/></diffuse></phong></technique>
    </profile_COMMON></effect>
  </library_effects>
  <library_materials><material id="Mat" name="body_a"><instance_effect url="#Effect"/></material></library_materials>
"""

VARIANT_A = """
  <library_geometries><geometry id="Geometry"><mesh>
    <source id="Pos"><float_array id="Pos-array" count="9">0 0 0 100 0 0 0 100 0</float_array>
      <technique_common><accessor source="#Pos-array" count="3" stride="3"/></technique_common></source>
    <source id="UV"><float_array id="UV-array" count="6">0 0 1 0 0 1</float_array>
      <technique_common><accessor source="#UV-array" count="3" stride="2"/></technique_common></source>
    <vertices id="V"><input semantic="POSITION" source="#Pos"/><input semantic="TEXCOORD" source="#UV"/></vertices>
    <triangles material="Sym0" count="1">
      <input semantic="VERTEX" source="#V" offset="0"/><p>0 1 2</p>
    </triangles>
  </mesh></geometry></library_geometries>
  <library_visual_scenes><visual_scene id="Scene"><node id="N"><instance_geometry url="#Geometry">
    <bind_material><technique_common><instance_material symbol="Sym0" target="#Mat"/></technique_common></bind_material>
  </instance_geometry></node></visual_scene></library_visual_scenes>
"""

VARIANT_B = """
  <library_geometries><geometry id="Geometry"><mesh>
    <source id="Pos"><float_array id="Pos-array" count="9">0 0 0 100 0 0 0 100 0</float_array>
      <technique_common><accessor source="#Pos-array" count="3" stride="3"/></technique_common></source>
    <source id="UV"><float_array id="UV-array" count="6">0 0 1 0 0 1</float_array>
      <technique_common><accessor source="#UV-array" count="3" stride="2"/></technique_common></source>
    <source id="N"><float_array id="N-array" count="9">0 0 1 0 0 1 0 0 1</float_array>
      <technique_common><accessor source="#N-array" count="3" stride="3"/></technique_common></source>
    <source id="C"><float_array id="C-array" count="12">1 1 1 1 1 1 1 1 1 1 1 1</float_array>
      <technique_common><accessor source="#C-array" count="3" stride="4"/></technique_common></source>
    <vertices id="V"><input semantic="POSITION" source="#Pos"/></vertices>
    <triangles material="Mat" count="1">
      <input semantic="VERTEX" source="#V" offset="0"/>
      <input semantic="NORMAL" source="#N" offset="1"/>
      <input semantic="TEXCOORD" source="#UV" offset="2" set="0"/>
      <input semantic="COLOR" source="#C" offset="3" set="0"/>
      <p>0 0 0 0  1 0 1 0  2 0 2 0</p>
    </triangles>
  </mesh></geometry></library_geometries>
"""


def write_dae(tmp_path: Path, variant: str, image_ref: str) -> Path:
    dae = tmp_path / "model.dae"
    dae.write_text(COLLADA_HEAD + COMMON_LIBRARIES.format(image=image_ref) + variant + "</COLLADA>", encoding="utf-8")
    return dae


def read_mcsm(path: Path) -> dict:
    data = path.read_bytes()
    offset = 0

    def u32() -> int:
        nonlocal offset
        value = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        return value

    def f32() -> float:
        nonlocal offset
        value = struct.unpack_from("<f", data, offset)[0]
        offset += 4
        return value

    def u16() -> int:
        nonlocal offset
        value = struct.unpack_from("<H", data, offset)[0]
        offset += 2
        return value

    def string() -> str:
        nonlocal offset
        length = u32()
        value = data[offset : offset + length].decode("utf-8")
        offset += length
        return value

    out = {"magic": u32(), "version": u32(), "vertex_count": u32(), "index_count": u32(), "material_count": u32()}
    out["identifier"] = string()
    out["yaw_offset"] = f32()
    out["materials"] = []
    for _ in range(out["material_count"]):
        out["materials"].append({"name": string(), "texture": string(), "first_index": u32(), "index_count": u32()})
    out["vertices"] = [(f32(), f32(), f32(), f32(), f32()) for _ in range(out["vertex_count"])]
    out["indices"] = [u16() for _ in range(out["index_count"])]
    return out


@pytest.fixture()
def tex(tmp_path: Path) -> Path:
    image = Image.new("RGBA", (4, 4), (255, 0, 0, 255))
    image.putpixel((0, 0), (0, 255, 0, 255))
    path = tmp_path / "tex_alb.png"
    image.save(path)
    return path


def test_variant_a_with_uv_inside_vertices(tmp_path: Path, tex: Path) -> None:
    dae = write_dae(tmp_path, VARIANT_A, "./tex_alb.png")
    out = tmp_path / "out"
    meta = bake(dae, "darkrai", out, 1.0, 0.0, 1024, True, True, "y")

    assert meta["unit_scale"] == 0.01  # 100 source units -> 1 m
    assert meta["vertices"] == 3
    assert meta["triangles"] == 1

    mesh = read_mcsm(out / "darkrai.mcsm")
    assert mesh["magic"] == MCSM_MAGIC
    assert mesh["version"] == MCSM_VERSION
    assert mesh["identifier"] == "darkrai"
    assert mesh["materials"][0]["texture"] == "darkrai.png"
    # centred on X/Z, feet at Y=0, in metres
    assert mesh["vertices"][0][:3] == pytest.approx((-0.5, 0.0, 0.0))
    assert mesh["vertices"][1][:3] == pytest.approx((0.5, 0.0, 0.0))
    assert mesh["vertices"][2][:3] == pytest.approx((-0.5, 1.0, 0.0))
    assert mesh["indices"] == [0, 1, 2]

    # UVs are V-flipped, then remapped to texel centres inside the 4x4 tile at (4,4)
    u0, v0 = mesh["vertices"][0][3:5]
    assert u0 == pytest.approx((4 + 0.5) / 1024)
    assert v0 == pytest.approx((4 + 0.5 + 3) / 1024)
    assert mesh["materials"][0]["index_count"] == 3


def test_variant_b_with_separate_texcoord_and_colour(tmp_path: Path, tex: Path) -> None:
    dae = write_dae(tmp_path, VARIANT_B, "tex_alb.png")
    meta = bake(dae, "charizard", tmp_path / "out", 1.0, 0.0, 1024, True, True, "y")
    assert meta["vertices"] == 3
    mesh = read_mcsm(tmp_path / "out" / "charizard.mcsm")
    assert mesh["vertices"][0][:3] == pytest.approx((-0.5, 0.0, 0.0))


def test_image_reference_is_url_decoded(tmp_path: Path) -> None:
    Image.new("RGBA", (4, 4), (0, 0, 255, 255)).save(tmp_path / "my tex_alb.png")
    dae = write_dae(tmp_path, VARIANT_A, "./my%20tex_alb.png")
    meta = bake(dae, "cresselia", tmp_path / "out", 1.0, 0.0, 1024, True, True, "y")
    assert meta["materials"]["Sym0"]["image"] == "my tex_alb.png"


def test_auxiliary_map_falls_back_to_albedo(tmp_path: Path) -> None:
    Image.new("RGBA", (4, 4), (0, 0, 255, 255)).save(tmp_path / "pm0001_body_a_alb_color.png")
    Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(tmp_path / "pm0001_body_a_lym_a.png")
    dae = write_dae(tmp_path, VARIANT_A, "./pm0001_body_a_lym_a.png")
    meta = bake(dae, "fallback", tmp_path / "out", 1.0, 0.0, 1024, True, True, "y")
    assert meta["materials"]["Sym0"]["image"] == "pm0001_body_a_alb_color.png"


def test_yaw_offset_is_written_in_radians(tmp_path: Path, tex: Path) -> None:
    dae = write_dae(tmp_path, VARIANT_A, "./tex_alb.png")
    bake(dae, "darkrai", tmp_path / "out", 1.0, 90.0, 1024, True, True, "y")
    mesh = read_mcsm(tmp_path / "out" / "darkrai.mcsm")
    assert mesh["yaw_offset"] == pytest.approx(1.5707963, rel=1e-5)


def test_no_flip_v_keeps_the_collada_origin(tmp_path: Path, tex: Path) -> None:
    dae = write_dae(tmp_path, VARIANT_A, "./tex_alb.png")
    bake(dae, "darkrai", tmp_path / "out", 1.0, 0.0, 1024, False, True, "y")
    mesh = read_mcsm(tmp_path / "out" / "darkrai.mcsm")
    u0, v0 = mesh["vertices"][0][3:5]
    assert v0 == pytest.approx((4 + 0.5) / 1024)


def test_negative_v_is_wrapped_before_the_flip(tmp_path: Path, tex: Path) -> None:
    # body_a/eye materials come out of the exporter with V shifted into -1..0
    dae = write_dae(tmp_path, VARIANT_A.replace("0 0 1 0 0 1", "0 -0.9 1 -0.9 0 -0.1"), "./tex_alb.png")
    bake(dae, "darkrai", tmp_path / "out", 1.0, 0.0, 1024, True, True, "y")
    mesh = read_mcsm(tmp_path / "out" / "darkrai.mcsm")
    u0, v0 = mesh["vertices"][0][3:5]
    assert v0 == pytest.approx((4 + 0.5 + 0.9 * 3) / 1024)


def test_v_above_one_is_wrapped_before_the_flip(tmp_path: Path, tex: Path) -> None:
    # body_c in Cresselia comes out with V shifted into 1..2
    dae = write_dae(tmp_path, VARIANT_A.replace("0 0 1 0 0 1", "0 1.9 1 1.9 0 1.1"), "./tex_alb.png")
    bake(dae, "cresselia", tmp_path / "out", 1.0, 0.0, 1024, True, True, "y")
    mesh = read_mcsm(tmp_path / "out" / "cresselia.mcsm")
    u0, v0 = mesh["vertices"][0][3:5]
    assert v0 == pytest.approx((4 + 0.5 + 0.1 * 3) / 1024)
