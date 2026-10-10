import json
import shutil
import tempfile
import zipfile
from pathlib import Path
import pytest

from tools.package_mod import ModPackager, load_spec


@pytest.fixture
def temp_workspace():
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        build_dir = root / "build"
        build_dir.mkdir()
        (build_dir / "dinput8.dll").write_bytes(b"MZ_FAKE_DLL_DATA")

        # Fake template file
        templates_dir = root / "tools" / "reverse" / "eldenring" / "templates"
        templates_dir.mkdir(parents=True)
        (templates_dir / "mc_er_hit.bin").write_bytes(b"FAKE_HIT_TEMPLATE_576_BYTES" * 20)

        # Fake placeholder texture
        source_tex_dir = root / "assets" / "source" / "textures"
        source_tex_dir.mkdir(parents=True)
        (source_tex_dir / "mc_hud_atlas.png").write_bytes(b"PNG_FAKE_ATLAS")

        out_dir = root / "dist"
        out_dir.mkdir()

        yield {
            "root": root,
            "build_dir": build_dir,
            "out_dir": out_dir,
            "hit_template": templates_dir / "mc_er_hit.bin",
        }


def test_load_spec(temp_workspace):
    root = temp_workspace["root"]
    spec_path = root / "package_specs" / "test_game.json"
    spec_path.parent.mkdir(parents=True, exist_ok=True)
    spec_data = {
        "game_id": "test_game",
        "display_name": "Test Game MC Mod",
        "dll_name": "dinput8.dll",
        "required_files": [],
        "default_configs": {"config.txt": "k=v\n"},
        "asset_dir": "mods/mc_adapter"
    }
    spec_path.write_text(json.dumps(spec_data), encoding="utf-8")

    spec = load_spec(spec_path)
    assert spec["game_id"] == "test_game"
    assert spec["dll_name"] == "dinput8.dll"


def test_package_eldenring_nexus_channel(temp_workspace):
    root = temp_workspace["root"]
    build_dir = temp_workspace["build_dir"]
    out_dir = temp_workspace["out_dir"]

    spec = {
        "game_id": "eldenring",
        "display_name": "Elden Ring Minecraft Mod",
        "dll_name": "dinput8.dll",
        "required_files": [
            {
                "src": str(temp_workspace["hit_template"].relative_to(root)),
                "dest": "mc_er_hit.bin"
            }
        ],
        "default_configs": {
            "mc_er_overlay.txt": "",
            "mc_er_damage.txt": "",
            "mc_er_input.txt": "",
            "mc_er_steve.txt": "occlusion=1\nblocks=1\n"
        },
        "asset_dir": "mods/mc_adapter",
        "readme_notes": ["Disable EAC before launching", "Supports Mod Engine 2 chain loading"]
    }

    packager = ModPackager(
        repo_root=root,
        spec=spec,
        build_dir=build_dir,
        version="0.1.0",
        channel="nexus"
    )

    result = packager.build_package(out_dir=out_dir, make_zip=True)

    assert result["success"] is True
    pkg_dir = Path(result["package_dir"])
    assert (pkg_dir / "dinput8.dll").exists()
    assert (pkg_dir / "mc_er_hit.bin").exists()
    assert (pkg_dir / "mc_er_steve.txt").exists()
    assert (pkg_dir / "README_INSTALL.txt").exists()
    assert (pkg_dir / "setup_assets.bat").exists()
    assert (pkg_dir / "SHA256SUMS.txt").exists()
    assert (pkg_dir / "mods" / "mc_adapter" / "mc_hud_atlas.png").exists()

    # Verify zip output
    zip_path = Path(result["zip_path"])
    assert zip_path.exists()
    with zipfile.ZipFile(zip_path, "r") as zf:
        namelist = zf.namelist()
        assert "dinput8.dll" in namelist
        assert "mc_er_hit.bin" in namelist
        assert "README_INSTALL.txt" in namelist


def test_package_missing_dll_raises_error(temp_workspace):
    root = temp_workspace["root"]
    empty_build_dir = root / "empty_build"
    empty_build_dir.mkdir()
    out_dir = temp_workspace["out_dir"]

    spec = {
        "game_id": "test_game",
        "dll_name": "non_existent.dll",
        "required_files": [],
        "default_configs": {},
        "asset_dir": "mods/mc_adapter"
    }

    packager = ModPackager(
        repo_root=root,
        spec=spec,
        build_dir=empty_build_dir,
        version="0.1.0",
        channel="nexus"
    )

    with pytest.raises(FileNotFoundError, match="Target DLL 'non_existent.dll' not found"):
        packager.build_package(out_dir=out_dir)


def test_package_missing_required_file_raises_error(temp_workspace):
    root = temp_workspace["root"]
    build_dir = temp_workspace["build_dir"]
    out_dir = temp_workspace["out_dir"]

    spec = {
        "game_id": "test_game",
        "dll_name": "dinput8.dll",
        "required_files": [{"src": "non/existent/file.bin", "dest": "file.bin"}],
        "default_configs": {},
        "asset_dir": "mods/mc_adapter"
    }

    packager = ModPackager(
        repo_root=root,
        spec=spec,
        build_dir=build_dir,
        version="0.1.0",
        channel="nexus"
    )

    with pytest.raises(FileNotFoundError, match="Required file not found"):
        packager.build_package(out_dir=out_dir)


def test_existing_package_specs_valid():
    repo_root = Path(__file__).resolve().parent.parent
    specs_dir = repo_root / "package_specs"
    assert (specs_dir / "eldenring.json").exists()
    assert (specs_dir / "sekiro.json").exists()

    er_spec = load_spec(specs_dir / "eldenring.json")
    assert er_spec["game_id"] == "eldenring"
    assert er_spec["dll_name"] == "dinput8.dll"
    # Verify required files exist in repo
    for req in er_spec.get("required_files", []):
        src_file = repo_root / req["src"]
        assert src_file.exists(), f"Elden Ring spec required file missing: {src_file}"

    sekiro_spec = load_spec(specs_dir / "sekiro.json")
    assert sekiro_spec["game_id"] == "sekiro"
    assert sekiro_spec["dll_name"] == "dinput8.dll"

    wukong_spec = load_spec(specs_dir / "wukong.json")
    assert wukong_spec["game_id"] == "wukong"
    assert wukong_spec["dll_name"] == "wukong_adapter.dll"

