from __future__ import annotations

import io
import json
import struct
import tempfile
import zipfile
from pathlib import Path
import pytest
from PIL import Image

import sys
REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.package_eldenring_mod import (
    verify_atlas_integrity,
    verify_skin_integrity,
    verify_geo_model,
    verify_audio_pack,
    verify_pe_dll,
    build_distribution_package,
    compute_sha256,
)


def test_verify_atlas_integrity_rejects_corrupted_or_empty_atlas(tmp_path: Path):
    # 1. Reject invalid dimension
    bad_dim = tmp_path / "bad_dim.png"
    Image.new("RGBA", (256, 256), (0, 0, 0, 0)).save(bad_dim)
    assert verify_atlas_integrity(bad_dim) is False

    # 2. Reject 512x512 atlas with empty shield
    empty_shield = tmp_path / "empty_shield.png"
    img = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    img.save(empty_shield)
    assert verify_atlas_integrity(empty_shield) is False


def test_verify_atlas_integrity_accepts_authentic_atlas():
    authentic_atlas = REPO_ROOT / "build" / "authentic_atlas" / "mc_hud_atlas.png"
    if not authentic_atlas.exists():
        authentic_atlas = REPO_ROOT / "assets" / "source" / "textures" / "mc_hud_atlas.authentic.png"
    assert authentic_atlas.exists(), f"Authentic atlas missing at {authentic_atlas}"
    assert verify_atlas_integrity(authentic_atlas) is True


def test_verify_skin_integrity(tmp_path: Path):
    # Reject 0-alpha or empty image
    blank_skin = tmp_path / "blank_steve.png"
    Image.new("RGBA", (64, 64), (0, 0, 0, 0)).save(blank_skin)
    assert verify_skin_integrity(blank_skin, 64, 64) is False

    # Accept real authentic creeper skin
    creeper_skin = REPO_ROOT / "assets" / "source" / "models" / "entities" / "creeper.authentic.png"
    if creeper_skin.exists():
        assert verify_skin_integrity(creeper_skin, 64, 32) is True


def test_verify_geo_models():
    models_dir = REPO_ROOT / "assets" / "source" / "models" / "entities"
    for name in ["creeper.geo.json", "skeleton.geo.json", "zombie.geo.json"]:
        p = models_dir / name
        assert p.exists(), f"Model {name} missing"
        assert verify_geo_model(p) is True


def test_verify_pe_dll():
    dll_path = REPO_ROOT / "build" / "bin" / "eldenring" / "dinput8.dll"
    if dll_path.exists():
        assert verify_pe_dll(dll_path) is True


def test_verify_audio_pack():
    game_sounds = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mods\mc_adapter\sounds")
    if game_sounds.is_dir():
        events_count, wav_count = verify_audio_pack(game_sounds)
        assert events_count >= 28
        assert wav_count >= 90


def test_build_distribution_package_e2e(tmp_path: Path):
    out_dir = tmp_path / "dist"
    result = build_distribution_package(
        repo_root=REPO_ROOT,
        out_dir=out_dir,
        version="1.0.0",
        game_dir=Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game"),
    )

    assert result["success"] is True
    zip_path = Path(result["zip_path"])
    assert zip_path.is_file()
    assert zip_path.stat().st_size > 1_000_000  # Full release with audio & DLL is > 1MB

    # Deep verification of ZIP archive structure
    with zipfile.ZipFile(zip_path, "r") as zf:
        namelist = set(zf.namelist())

        # Essential root files
        assert "dinput8.dll" in namelist
        assert "mc_hud_atlas.png" in namelist
        assert "mc_er_hit.bin" in namelist
        assert "mc_er_bullet.bin" in namelist
        assert "steam_appid.txt" in namelist
        assert "mc_er_steve.txt" in namelist
        assert "mc_er_damage.txt" in namelist
        assert "mc_er_overlay.txt" in namelist
        assert "mc_er_input.txt" in namelist
        assert "mc_er_summon.txt" in namelist
        assert "mc_er_summonhide.txt" in namelist
        assert "mc_er_bulletfire.txt" in namelist
        assert "README.md" in namelist
        assert "README_CN.md" in namelist
        assert "README_INSTALL.txt" in namelist
        assert "使用说明_中文.txt" in namelist
        assert "SHA256SUMS.txt" in namelist

        # Verify Chinese documentation contents
        readme_cn_bytes = zf.read("README_CN.md")
        assert len(readme_cn_bytes) > 2000
        assert "艾尔登法环 Minecraft Mod" in readme_cn_bytes.decode("utf-8")

        # Essential mod assets
        assert "mods/mc_adapter/mc_hud_atlas.png" in namelist
        assert "mods/mc_adapter/steve.png" in namelist
        assert "mods/mc_adapter/creeper.png" in namelist
        assert "mods/mc_adapter/models/creeper.geo.json" in namelist
        assert "mods/mc_adapter/models/skeleton.geo.json" in namelist
        assert "mods/mc_adapter/models/zombie.geo.json" in namelist
        assert "mods/mc_adapter/sounds/sounds_manifest.txt" in namelist

        # Audio files verification
        wav_files = [n for n in namelist if n.endswith(".wav")]
        assert len(wav_files) >= 90

        # Check CRC and corruptions
        bad_file = zf.testzip()
        assert bad_file is None, f"Corrupted file inside zip: {bad_file}"

        # Check SHA256SUMS.txt matches actual contents
        manifest_text = zf.read("SHA256SUMS.txt").decode("utf-8")
        manifest_entries = {}
        for line in manifest_text.strip().splitlines():
            parts = line.split("  ", 1)
            if len(parts) == 2:
                manifest_entries[parts[1]] = parts[0]

        for arcname in namelist:
            if arcname == "SHA256SUMS.txt" or arcname.endswith("/"):
                continue
            assert arcname in manifest_entries, f"Missing {arcname} in SHA256SUMS.txt"
            content = zf.read(arcname)
            computed = compute_sha256(content)
            assert computed.lower() == manifest_entries[arcname].lower(), f"Hash mismatch for {arcname}"
