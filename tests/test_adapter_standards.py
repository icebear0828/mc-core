import os
import re
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
ADAPTERS_DIR = REPO_ROOT / "adapters"

def test_hud_rendering_has_no_shoddy_text_placeholders():
    """
    Rule: HUD rendering must never use text substitutes like '<3', '()', 'Sword'
    for hearts, hunger, or item icons. Authentic texture atlas UVs must be used.
    """
    forbidden_patterns = [
        re.compile(r'AddText\([^)]*["\']<3["\']'),
        re.compile(r'AddText\([^)]*["\']\(\)["\']'),
        re.compile(r'AddText\([^)]*["\']Sword["\']'),
    ]

    for root, _, files in os.walk(ADAPTERS_DIR):
        for file in files:
            if file.endswith((".cpp", ".h", ".hpp")):
                path = Path(root) / file
                content = path.read_text(encoding="utf-8", errors="ignore")
                for pattern in forbidden_patterns:
                    matches = pattern.findall(content)
                    assert not matches, f"Found shoddy HUD placeholder '{matches}' in {path}"

def test_d3d_hooks_intercept_resize_buffers():
    """
    Rule: Any Direct3D hooking loader must hook ResizeBuffers to handle
    fullscreen/resolution switches without detaching or leaking back buffers.
    """
    for root, _, files in os.walk(ADAPTERS_DIR):
        for file in files:
            if "loader" in file and file.endswith(".cpp"):
                path = Path(root) / file
                content = path.read_text(encoding="utf-8", errors="ignore")
                if "IDXGISwapChain" in content:
                    assert "ResizeBuffers" in content, (
                        f"{path} hooks DXGI swapchain but does not intercept ResizeBuffers"
                    )

def test_d3d_hooks_dynamically_acquire_backbuffer():
    """
    Rule: Modern commercial games use DXGI Flip Model. RTV must be acquired
    and released per frame via GetBuffer(0, ...), not statically cached once.
    """
    for root, _, files in os.walk(ADAPTERS_DIR):
        for file in files:
            if "loader" in file and file.endswith(".cpp"):
                path = Path(root) / file
                content = path.read_text(encoding="utf-8", errors="ignore")
                if "IDXGISwapChain" in content:
                    assert "GetBuffer" in content, (
                        f"{path} does not call GetBuffer per-frame for flip model back buffers"
                    )

def test_hud_atlas_header_exists_and_valid():
    """
    Rule: HUD textures must be provided via generated atlas header.
    """
    atlas_header = ADAPTERS_DIR / "sekiro" / "include" / "sekiro_hud_atlas.hpp"
    assert atlas_header.exists(), "Sekiro HUD atlas header does not exist"
    content = atlas_header.read_text(encoding="utf-8")
    assert "kHudAtlasPngData" in content
    assert "kUV_HEART_FULL" in content
    assert "kUV_HUNGER_FULL" in content
    assert "kUV_CROSSHAIR" in content
