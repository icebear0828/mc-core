"""Guards for the extracted Steve HLSL shader and the slimmed D3D12 renderer (issue #99)."""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "adapters" / "eldenring" / "shaders"
SHADER = SHADER_DIR / "steve.hlsl"
TEMPLATE = SHADER_DIR / "steve_shader_source.hpp.in"
RENDERER = ROOT / "adapters" / "eldenring" / "src" / "steve_renderer_d3d12.cpp"

# SHA-256 of the raw string body at bd3b870. The move must be byte-for-byte.
SHADER_SHA256 = "529e4541a689c1c03d19750c7c0809c2743c12bbf68e3eaf44bb1ed667342a39"

ENTRY_POINTS = ["VSMain", "VSFull", "PSMain", "PSDepthView", "PSShadow"]


def test_shader_files_exist() -> None:
    assert SHADER.is_file(), f"missing {SHADER}"
    assert TEMPLATE.is_file(), f"missing {TEMPLATE}"
    template = TEMPLATE.read_text(encoding="utf-8")
    assert "@MC_STEVE_HLSL_SOURCE@" in template
    assert 'R"hlsl(' in template


def test_entry_points_present() -> None:
    source = SHADER.read_text(encoding="utf-8")
    for entry in ENTRY_POINTS:
        assert entry in source, f"entry point {entry} missing from steve.hlsl"
    # There is no VSShadow: the shadow pipeline reuses VSMain.
    assert "VSShadow" not in source


def test_no_inline_hlsl_in_renderer() -> None:
    source = RENDERER.read_text(encoding="utf-8")
    assert 'R"hlsl(' not in source
    assert "kShader" not in source
    assert "D3DCompile(" not in source


def test_line_budget() -> None:
    lines = len(RENDERER.read_text(encoding="utf-8").splitlines())
    assert lines < 600, f"steve_renderer_d3d12.cpp has {lines} lines, expected < 600"


def test_shader_source_hash() -> None:
    digest = hashlib.sha256(SHADER.read_bytes()).hexdigest()
    assert digest == SHADER_SHA256, "the shader body changed during the move; it must stay byte-identical"


def _find_fxc() -> str | None:
    found = shutil.which("fxc")
    if found:
        return found
    if sys.platform != "win32":
        return None
    kits = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "bin"
    if not kits.is_dir():
        return None
    candidates = sorted(kits.glob("*/x64/fxc.exe"))
    return str(candidates[-1]) if candidates else None


def test_fxc_compiles_all_entries(tmp_path: Path) -> None:
    fxc = _find_fxc()
    if not fxc:
        pytest.skip("fxc not available")
    profiles = {"VSMain": "vs_5_0", "VSFull": "vs_5_0", "PSMain": "ps_5_0", "PSDepthView": "ps_5_0", "PSShadow": "ps_5_0"}
    for entry, profile in profiles.items():
        out = tmp_path / f"{entry}.cso"
        result = subprocess.run(
            [fxc, "/nologo", "/T", profile, "/E", entry, "/Fo", str(out), str(SHADER)],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, f"fxc failed for {entry} ({profile}): {result.stdout}\n{result.stderr}"
