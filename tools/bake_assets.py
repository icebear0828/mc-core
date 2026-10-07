"""Cross-Engine Asset Baking & Packaging Automation Pipeline."""
from __future__ import annotations
import argparse
import platform
import shutil
import subprocess
from pathlib import Path


SUPPORTED_TARGETS = ["ue5", "cp2077", "gta5", "eldenring", "re_engine", "sekiro"]


def check_tool(tool_name: str) -> bool:
    return shutil.which(tool_name) is not None


def bake_ue5(source_dir: Path, output_dir: Path) -> bool:
    print(f"\n[UE5] Baking Unreal Engine 5 .pak package...")
    ue5_out = output_dir / "ue5"
    ue5_out.mkdir(parents=True, exist_ok=True)

    repak_tool = shutil.which("repak")
    if not repak_tool:
        print("[UE5] 'repak' CLI not found in PATH.")
        print("      To install repak (Rust): cargo install repak")
        print("      Manual Packaging fallback: Use UnrealPak.exe or repak to package assets into mc_assets_P.pak")
        return False

    # Package loose assets into .pak using repak
    pak_file = ue5_out / "mc_assets_P.pak"
    cmd = [repak_tool, "pack", str(source_dir), str(pak_file)]
    print(f"Executing: {' '.join(cmd)}")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print(f"[UE5] Successfully created: {pak_file}")
        return True
    else:
        print(f"[UE5] repak failed: {res.stderr}")
        return False


def bake_cp2077(source_dir: Path, output_dir: Path) -> bool:
    print(f"\n[CP2077] Baking Cyberpunk 2077 .archive package...")
    cp_out = output_dir / "cp2077"
    cp_out.mkdir(parents=True, exist_ok=True)

    wk_cli = shutil.which("wk-cli") or shutil.which("wolvenkit-cli")
    if not wk_cli:
        print("[CP2077] 'wk-cli' (WolvenKit CLI) not found in PATH.")
        print("         To install: Download WolvenKit CLI from https://github.com/WolvenKit/WolvenKit/releases")
        print("         Workflow: Import OBJs from source_dir, export .mesh, run wk-cli pack.")
        return False

    archive_file = cp_out / "mc_assets.archive"
    cmd = [wk_cli, "pack", "-i", str(source_dir), "-o", str(archive_file)]
    print(f"Executing: {' '.join(cmd)}")
    res = subprocess.run(cmd, capture_output=True, text=True)
    return res.returncode == 0


def bake_gta5(source_dir: Path, output_dir: Path) -> bool:
    print(f"\n[GTA5] Baking GTA V RAGE engine .ydr / .rpf package...")
    gta_out = output_dir / "gta5"
    gta_out.mkdir(parents=True, exist_ok=True)

    print("[GTA5] Exporting Sollumz intermediate definition layout...")
    # Copy models and textures into GTA-ready directory
    for f in source_dir.glob("models/*.obj"):
        shutil.copy(f, gta_out)
    for f in source_dir.glob("textures/*.png"):
        shutil.copy(f, gta_out)

    print("       To finalize on Windows: Import into Blender via Sollumz plugin and export as .ydr/.ytd,")
    print("       or use OpenIV to generate mc_assets.rpf archive.")
    return True


def bake_sekiro(source_dir: Path, output_dir: Path) -> bool:
    print(f"\n[SEKIRO] Baking Sekiro (Dantelion / ModEngine) asset package...")
    sekiro_out = output_dir / "sekiro"
    parts_out = sekiro_out / "parts"
    map_out = sekiro_out / "map"
    parts_out.mkdir(parents=True, exist_ok=True)
    map_out.mkdir(parents=True, exist_ok=True)

    print("         Staging 12 Steve parts for FLVER character parts conversion...")
    for f in source_dir.glob("models/steve*.obj"):
        shutil.copy(f, parts_out)
    for f in source_dir.glob("textures/steve*.png"):
        shutil.copy(f, parts_out)

    print("         Staging blocks and weapons for map / parts container...")
    for f in source_dir.glob("models/*.obj"):
        if not f.name.startswith("steve"):
            shutil.copy(f, map_out)
    for f in source_dir.glob("textures/*.png"):
        if not f.name.startswith("steve"):
            shutil.copy(f, map_out)

    # Build and stage HUD atlas texture for D3D11 overlay
    try:
        import sys
        sys.path.insert(0, str(Path(__file__).parent))
        from extract_mc_assets import build_hud_atlas
        atlas_img, _ = build_hud_atlas()
        atlas_img.save(sekiro_out / "mc_hud_atlas.png")
        print("         Staged mc_hud_atlas.png for Sekiro D3D11 HUD overlay.")
    except Exception as e:
        print(f"         Notice: HUD atlas generation skipped: {e}")

    print(f"[SEKIRO] Staged {len(list(parts_out.glob('*')))} part files and {len(list(map_out.glob('*')))} map files.")
    return True


def main():
    parser = argparse.ArgumentParser(description="Bake source assets into engine-specific binary packages.")
    parser.add_argument("--source-dir", type=Path, default=Path("assets/source"), help="Path to source OBJ/PNG assets")
    parser.add_argument("--output-dir", type=Path, default=Path("assets/cooked"), help="Output directory for packages")
    parser.add_argument("--target", choices=SUPPORTED_TARGETS + ["all"], default="all", help="Target engine package")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    current_os = platform.system()
    print(f"MC Asset Baking Pipeline (Running on {current_os} {platform.machine()})")
    print(f"Source Directory: {args.source_dir.resolve()}")
    print(f"Output Directory: {args.output_dir.resolve()}\n")

    targets = SUPPORTED_TARGETS if args.target == "all" else [args.target]

    for t in targets:
        if t == "ue5":
            bake_ue5(args.source_dir, args.output_dir)
        elif t == "cp2077":
            bake_cp2077(args.source_dir, args.output_dir)
        elif t == "gta5":
            bake_gta5(args.source_dir, args.output_dir)
        elif t == "sekiro":
            bake_sekiro(args.source_dir, args.output_dir)
        elif t in ["eldenring", "re_engine"]:
            dest = args.output_dir / t
            dest.mkdir(parents=True, exist_ok=True)
            print(f"[{t.upper()}] Prepped asset staging directory at {dest}")

    print("\nBaking pipeline staging complete.")


if __name__ == "__main__":
    main()
