from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.package_modes_local import _classify, _is_ignored, build_bundle


def _write(path: Path, data: bytes = b"x") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


@pytest.fixture
def game_dir(tmp_path: Path) -> Path:
    mods = tmp_path / "mods" / "mc_adapter"
    mc = [
        mods / "mc_hud_atlas.png",
        mods / "steve.png",
        mods / "models" / "zombie.geo.json",
        mods / "sounds" / "hit.wav",
        mods / "textures" / "armor" / "diamond_humanoid.png",
        mods / "mc_hud_atlas.png.bak_modes_20260101_000000",
        mods / "inventory.txt",
        tmp_path / "mc_er_steve.txt",
        tmp_path / "mc_er_hit.bin",
        tmp_path / "mc_er_dbg_anim.txt",
    ]
    cs = [
        mods / "cs_ak47_viewmodel.bin",
        mods / "textures" / "ak47" / "ak47_color.png",
        mods / "sounds" / "ak47_02.wav",
        mods / "sounds" / "sounds_manifest.txt",
    ]
    ow = [
        mods / "ow_gauntlet.bin",
        mods / "ow_gauntlet.png",
        tmp_path / "mc_er_ow.txt",
    ]
    for p in mc + cs + ow:
        _write(p)

    manifest = {
        "version": 1,
        "modes": ["mc", "cs", "ow"],
        "start_mode": "mc",
        "files": {
            "cs": [{"dest": str(p)} for p in cs],
            "ow": [{"dest": str(p)} for p in ow],
            "dll": [{"dest": str(tmp_path / "dinput8.dll")}],
        },
    }
    _write(mods / "mode_manifest.json", json.dumps(manifest).encode("utf-8"))
    return tmp_path


def _rel_files(root: Path) -> set[str]:
    return {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}


def test_build_bundle_mc_excludes_cs_ow_and_runtime_junk(game_dir: Path, tmp_path: Path):
    manifest = json.loads((game_dir / "mods" / "mc_adapter" / "mode_manifest.json").read_text(encoding="utf-8"))
    bundle = tmp_path / "bundle_mc"
    build_bundle(game_dir, manifest, ("mc",), bundle)
    files = _rel_files(bundle)

    assert "mods/mc_adapter/mc_hud_atlas.png" in files
    assert "mods/mc_adapter/steve.png" in files
    assert "mods/mc_adapter/models/zombie.geo.json" in files
    assert "mods/mc_adapter/sounds/hit.wav" in files
    assert "mc_er_steve.txt" in files
    assert "mc_er_hit.bin" in files

    assert "mods/mc_adapter/cs_ak47_viewmodel.bin" not in files
    assert "mods/mc_adapter/ow_gauntlet.bin" not in files
    assert "mc_er_ow.txt" not in files
    assert "mc_er_dbg_anim.txt" not in files
    assert "mods/mc_adapter/inventory.txt" not in files
    assert not any(".bak" in f for f in files)


def test_build_bundle_cs_and_ow_are_isolated(game_dir: Path, tmp_path: Path):
    manifest = json.loads((game_dir / "mods" / "mc_adapter" / "mode_manifest.json").read_text(encoding="utf-8"))

    cs_bundle = tmp_path / "bundle_cs"
    build_bundle(game_dir, manifest, ("cs",), cs_bundle)
    cs_files = _rel_files(cs_bundle)
    assert "mods/mc_adapter/cs_ak47_viewmodel.bin" in cs_files
    assert "mods/mc_adapter/textures/ak47/ak47_color.png" in cs_files
    assert "mods/mc_adapter/sounds/sounds_manifest.txt" in cs_files
    assert "mods/mc_adapter/mc_hud_atlas.png" not in cs_files
    assert "mods/mc_adapter/ow_gauntlet.bin" not in cs_files

    ow_bundle = tmp_path / "bundle_ow"
    build_bundle(game_dir, manifest, ("ow",), ow_bundle)
    ow_files = _rel_files(ow_bundle)
    assert "mods/mc_adapter/ow_gauntlet.bin" in ow_files
    assert "mc_er_ow.txt" in ow_files
    assert "mods/mc_adapter/mc_hud_atlas.png" not in ow_files
    assert "mods/mc_adapter/cs_ak47_viewmodel.bin" not in ow_files


def test_build_bundle_all_is_the_union(game_dir: Path, tmp_path: Path):
    manifest = json.loads((game_dir / "mods" / "mc_adapter" / "mode_manifest.json").read_text(encoding="utf-8"))
    bundle = tmp_path / "bundle_all"
    build_bundle(game_dir, manifest, ("mc", "cs", "ow"), bundle)
    files = _rel_files(bundle)
    assert "mods/mc_adapter/mc_hud_atlas.png" in files
    assert "mods/mc_adapter/cs_ak47_viewmodel.bin" in files
    assert "mods/mc_adapter/ow_gauntlet.bin" in files
    assert "mc_er_ow.txt" in files
    assert "mc_er_dbg_anim.txt" not in files


def test_classify_and_ignore_rules():
    assert _is_ignored("mc_hud_atlas.png.bak_modes_20260101")
    assert _is_ignored("backup_before_doomfist")
    assert _is_ignored("dinput8.dll")
    assert _is_ignored("mode_manifest.json")
    assert _is_ignored("inventory.txt")
    assert not _is_ignored("mc_hud_atlas.png")

    cs = set()
    ow = set()
    assert _classify(Path("mc_er_ow.txt"), cs, ow) == "ow"
    assert _classify(Path("sounds/sounds_manifest.txt"), cs, ow) == "cs"
    assert _classify(Path("cs_ak47_viewmodel.bin"), cs, ow) == "cs"
    assert _classify(Path("ow_gauntlet.bin"), cs, ow) == "ow"
    assert _classify(Path("mc_hud_atlas.png"), cs, ow) == "mc"
