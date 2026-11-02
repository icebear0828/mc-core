"""Extract Minecraft Java assets into standard OBJ/PNG 3D models for cross-engine importing.

This module is the stable CLI facade. The implementation lives in ``asset_pipeline``;
every name the old single-file module exposed is re-exported here for compatibility.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

_TOOLS_DIR = Path(__file__).resolve().parent
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

from PIL import Image  # noqa: E402

from asset_pipeline.atlas.canonical_sprites import _PLACEHOLDER_ITEM_COLORS, create_canonical_sprite  # noqa: E402
from asset_pipeline.atlas.hud_atlas import build_hud_atlas  # noqa: E402
from asset_pipeline.codegen.header_writer import export_adapter_atlas_header, export_hud_atlas_header  # noqa: E402
from asset_pipeline.core.constants import (  # noqa: E402
    HUD_SPRITES,
    PARTS,
    _CONTAINER_PARTS,
    _CONTAINER_TEXTURE,
    _CUBE_SHADE,
    _GLINT,
    _ITEM_KEYS,
    _JAR_BLOCK_ITEMS,
    _JAR_HUD_SPRITES,
    _JAR_TEXTURES,
    _LIMB_BLOCKS,
    _MOB_SKINS,
    _SPAWN_EGG_COLOURS,
)
from asset_pipeline.geometry.bedrock_parser import (  # noqa: E402
    _cube_faces_bedrock,
    build_geometry_mesh,
    parse_geometry_json,
)
from asset_pipeline.geometry.cuboid import build_steve_parts, cuboid, mesh_from_faces  # noqa: E402
from asset_pipeline.geometry.obj_exporter import export_obj  # noqa: E402
from asset_pipeline.textures.skins import (  # noqa: E402
    _limb_rects,
    export_entity_models,
    export_mob_skin,
    export_steve_skin,
    mirror_empty_left_limbs,
)
from asset_pipeline.textures.voxel_renderer import (  # noqa: E402
    _multiply_tint,
    _paste_face,
    _shade,
    _tint,
    build_cube_block,
    isometric_block_icon,
)

__all__ = [
    "PARTS",
    "HUD_SPRITES",
    "cuboid",
    "mesh_from_faces",
    "export_obj",
    "parse_geometry_json",
    "build_geometry_mesh",
    "build_steve_parts",
    "build_cube_block",
    "create_canonical_sprite",
    "build_hud_atlas",
    "export_hud_atlas_header",
    "export_adapter_atlas_header",
    "export_steve_skin",
    "export_mob_skin",
    "mirror_empty_left_limbs",
    "export_entity_models",
    "isometric_block_icon",
]


def main():
    parser = argparse.ArgumentParser(description="Extract MC Java assets to standard OBJ/PNG models.")
    parser.add_argument("--client-jar", type=Path, help="Path to Minecraft Java client.jar")
    parser.add_argument("--out-dir", type=Path, default=Path("assets/exported"), help="Output directory")
    parser.add_argument("--export-hud-atlas", action="store_true", help="Export mc_hud_atlas.png and UV JSON")
    parser.add_argument("--export-header", type=Path, help="Export the shared C++ HUD atlas header (include/mc/hud_atlas.hpp)")
    parser.add_argument("--export-adapter-header", type=Path, help="Export an adapter header forwarding the shared atlas names")
    parser.add_argument("--adapter-namespace", default="sekiro::hud", help="Namespace for --export-adapter-header")
    parser.add_argument("--export-steve-skin", action="store_true", help="Export the real Steve skin as steve.png (needs --client-jar)")
    parser.add_argument("--export-mob-skin", action="append", choices=sorted(_MOB_SKINS), help="Export a mob's skin as <mob>.png (needs --client-jar; repeatable)")
    parser.add_argument("--export-all-mobs", action="store_true", help="Export all supported mob skins (needs --client-jar)")
    parser.add_argument("--export-entity-models", action="store_true", help="Export all entity geometry JSONs (.geo.json) to --out-dir")
    parser.add_argument("--geometry-json", type=Path, help="Path to a Bedrock/Blockbench geometry.json to extract OBJ models from")
    parser.add_argument("--geometry-texture", type=Path, help="Optional texture PNG for --geometry-json")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.export_entity_models:
        root = Path(__file__).resolve().parent.parent
        copied = export_entity_models(root / "assets/source/models", args.out_dir)
        print(f"Exported {len(copied)} entity geometry models to {args.out_dir}")
    if args.geometry_json:
        geo = parse_geometry_json(args.geometry_json.read_text(encoding="utf-8"))
        tex = Image.open(args.geometry_texture).convert("RGBA") if args.geometry_texture else None
        bone_meshes = build_geometry_mesh(geo, tex)
        for bone_name, mesh in bone_meshes.items():
            obj_content = export_obj(f"{geo['identifier']}_{bone_name}", mesh)
            (args.out_dir / f"{bone_name}.obj").write_text(obj_content, encoding="utf-8")
        print(f"Exported {len(bone_meshes)} bone meshes from {args.geometry_json} to {args.out_dir}")
    if args.export_steve_skin:
        if not args.client_jar:
            parser.error("--export-steve-skin requires --client-jar")
        print(f"Exported Steve skin to {export_steve_skin(args.client_jar, args.out_dir)}")
    mobs_to_export = list(args.export_mob_skin or [])
    if args.export_all_mobs:
        mobs_to_export = sorted(_MOB_SKINS.keys())
    for mob in mobs_to_export:
        if not args.client_jar:
            parser.error("--export-mob-skin / --export-all-mobs requires --client-jar")
        print(f"Exported {mob} skin to {export_mob_skin(args.client_jar, args.out_dir, mob)}")
    if args.export_hud_atlas or args.export_header or args.export_adapter_header:

        atlas_img, uv_map = build_hud_atlas(args.client_jar)
        atlas_path = args.out_dir / "mc_hud_atlas.png"
        atlas_img.save(atlas_path)
        print(f"Exported HUD atlas to {atlas_path} with {len(uv_map)} UV regions.")

        if args.export_header:
            export_hud_atlas_header(atlas_img, uv_map, args.export_header)
            print(f"Exported C++ HUD header to {args.export_header}")
        if args.export_adapter_header:
            export_adapter_atlas_header(uv_map, args.export_adapter_header, args.adapter_namespace)
            print(f"Exported adapter HUD header to {args.export_adapter_header}")

    print(f"MC asset exporter ready. Output: {args.out_dir}")


if __name__ == "__main__":
    main()
