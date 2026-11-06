#!/usr/bin/env python3
"""Package the four Elden Ring mode variants from a local game install.

The game directory must already contain the extracted assets and the
``mode_manifest.json`` written by ``tools/deploy_modes.py``. This script only
reads those files and builds the packages; game-derived assets never enter the
repository.

Usage (from mc-core)::

    uv run python tools/package_modes_local.py --out-dir dist_local

The four outputs are ``mc_eldenring_{mc,cs,ow,mc_cs_ow}_v<version>_<channel>.zip``.
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Iterable, Sequence

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.package_mod import ModPackager, load_spec, parse_modes

DEFAULT_GAME_DIR = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")

# The four combinations, in the order the release pipeline uses them.
COMBOS = (
    ("mc", ("mc",), "mc"),
    ("cs", ("cs",), "cs"),
    ("ow", ("ow",), "ow"),
    ("all", ("mc", "cs", "ow"), "mc+cs+ow"),
)

# Root files that belong to the MC package. Debug/log toggles are deliberately
# left out; they are per-machine experiments, not distribution config.
MC_ROOT_FILES = {
    "mc_er_hit.bin",
    "mc_er_bullet.bin",
    "mc_er_steve.txt",
    "mc_er_overlay.txt",
    "mc_er_input.txt",
    "mc_er_damage.txt",
    "mc_er_summon.txt",
    "mc_er_summonhide.txt",
    "mc_er_bulletfire.txt",
}

# Root files the CS deployer writes outside the manifest (older layouts) and
# that must not leak into the MC package.
CS_ROOT_PREFIXES = ("cs_", "glove_hardknuckle", "knife_butterfly", "bare_arm", "dinput8_cs")
OW_ROOT_PREFIXES = ("ow_",)

EXCLUDED_NAMES = {"mode_manifest.json", "inventory.txt"}


def _is_ignored(name: str) -> bool:
    lower = name.lower()
    return (
        ".bak" in lower
        or lower.startswith("backup")
        or lower.startswith("dinput8")
        or lower.startswith("steve.bbmodel")
        or lower.endswith(".log")
        or lower in EXCLUDED_NAMES
    )


def _manifest_paths(manifest: dict, mode: str, game_dir: Path) -> set[Path]:
    paths: set[Path] = set()
    for entry in manifest.get("files", {}).get(mode, []):
        dest = entry.get("dest")
        if not dest:
            continue
        try:
            paths.add(Path(dest).resolve().relative_to(game_dir.resolve()))
        except ValueError:
            pass
    return paths


def _classify(rel: Path, cs: set[Path], ow: set[Path]) -> str:
    if rel in cs:
        return "cs"
    if rel in ow:
        return "ow"
    name = rel.name.lower()
    if name == "mc_er_ow.txt":
        return "ow"
    if name == "sounds_manifest.txt":
        # The CS deployer owns the shared sound manifest; MC sounds are read
        # from the directory itself.
        return "cs"
    if name.startswith(CS_ROOT_PREFIXES):
        return "cs"
    if name.startswith(OW_ROOT_PREFIXES):
        return "ow"
    return "mc"


def _under_mod_tree(rel: Path) -> bool:
    return len(rel.parts) >= 3 and rel.parts[0] == "mods" and rel.parts[1] == "mc_adapter"


def build_bundle(game_dir: Path, manifest: dict, selected: Sequence[str], bundle: Path) -> int:
    cs = _manifest_paths(manifest, "cs", game_dir)
    ow = _manifest_paths(manifest, "ow", game_dir)
    copied = 0
    for src in sorted(game_dir.rglob("*")):
        if not src.is_file():
            continue
        rel = src.relative_to(game_dir)
        if _is_ignored(rel.name):
            continue
        under_mods = _under_mod_tree(rel)
        if not under_mods and rel.name not in MC_ROOT_FILES and rel.name != "mc_er_ow.txt":
            continue
        mode = _classify(rel, cs, ow)
        if mode not in selected:
            continue
        dest = bundle / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        copied += 1
    return copied


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-dir", type=Path, default=DEFAULT_GAME_DIR, help="Elden Ring Game directory")
    parser.add_argument("--build-root", type=Path, default=REPO_ROOT / "build_modes", help="Directory holding build_modes/<combo>")
    parser.add_argument("--out-dir", type=Path, default=REPO_ROOT / "dist_local", help="Output directory for the four zips")
    parser.add_argument("--version", default="0.1.0", help="Version tag")
    parser.add_argument("--channel", choices=["nexus", "full"], default="nexus", help="Distribution channel")
    parser.add_argument("--only", help="Optional comma list of mc,cs,ow,all (default: all four)")
    parser.add_argument("--no-zip", action="store_true", help="Keep the package directories, do not zip")
    args = parser.parse_args(list(argv) if argv is not None else None)

    game_dir = args.game_dir.resolve()
    if not game_dir.is_dir():
        print(f"[ERROR] Game directory not found: {game_dir}", file=sys.stderr)
        return 1
    manifest_path = game_dir / "mods" / "mc_adapter" / "mode_manifest.json"
    if not manifest_path.is_file():
        print(f"[ERROR] {manifest_path} not found; run tools/deploy_modes.py --modes all --deploy first", file=sys.stderr)
        return 1
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    spec_path = REPO_ROOT / "package_specs" / "eldenring.json"
    spec = load_spec(spec_path)
    wanted = set(parse_modes(args.only)) if args.only else {"mc", "cs", "ow"}

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for label, modes, build_combo in COMBOS:
        if label != "all" and label not in wanted:
            continue
        if label == "all" and wanted != {"mc", "cs", "ow"}:
            continue
        build_dir = args.build_root / build_combo / "bin" / "eldenring"
        if not (build_dir / "dinput8.dll").is_file():
            print(f"[ERROR] Missing {build_dir / 'dinput8.dll'}; build {build_combo} first", file=sys.stderr)
            return 1
        with tempfile.TemporaryDirectory(prefix=f"eldenring-{label}-") as tmp:
            bundle = Path(tmp) / "bundle"
            copied = build_bundle(game_dir, manifest, modes, bundle)
            packager = ModPackager(
                repo_root=REPO_ROOT,
                spec=spec,
                build_dir=build_dir,
                version=args.version,
                channel=args.channel,
                modes=modes,
                start_mode=modes[0],
                assets_dir=bundle,
            )
            result = packager.build_package(out_dir=args.out_dir, make_zip=not args.no_zip)
            print(f"[PACKAGE] {label}: {result['zip_path'] or result['package_dir']} ({copied} asset files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
