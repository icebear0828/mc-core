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


SKILL_ASSETS = REPO_ROOT / "skills" / "mc-game-adapter" / "assets"


def test_plugin_entry_template_is_built_on_session_not_singletons():
    """A new game must start from the Session-based entry, not the old hand-written tick."""
    template = (SKILL_ASSETS / "plugin-entry.template.cpp").read_text(encoding="utf-8")
    assert "mc::Session" in template
    assert "GetGlobal" not in template
    assert "SteveAnimator" not in template, "tick orchestration belongs to Session"
    assert "on_ground_is_estimate" in template


def test_adapter_templates_implement_the_current_contract():
    header = (SKILL_ASSETS / "adapter-header.template.hpp").read_text(encoding="utf-8")
    source = (SKILL_ASSETS / "adapter-source.template.cpp").read_text(encoding="utf-8")
    for method in ("setSteveRoot", "setLinearVelocity"):
        assert method in header and method in source, f"template is missing {method}"
    assert "uint64_t entity_id" not in header and "uint64_t entity_id" not in source, "ids are EntityId now"


def test_docs_point_new_games_at_the_playbook():
    playbook = REPO_ROOT / "docs" / "PORTING_PLAYBOOK.md"
    assert playbook.exists()
    for doc in (REPO_ROOT / "docs" / "ARCHITECTURE.md", REPO_ROOT / "docs" / "ADAPTER_SPECIFICATION.md",
                REPO_ROOT / "skills" / "mc-game-adapter" / "SKILL.md"):
        assert "PORTING_PLAYBOOK" in doc.read_text(encoding="utf-8"), f"{doc.name} should link the playbook"


def test_playbook_references_only_files_that_exist():
    text = (REPO_ROOT / "docs" / "PORTING_PLAYBOOK.md").read_text(encoding="utf-8")
    for ref in sorted(set(re.findall(r"`((?:docs|tools|include|src|tests|skills)/[A-Za-z0-9_./-]+)`", text))):
        if "*" in ref or ref.endswith("/"):
            continue
        assert (REPO_ROOT / ref).exists(), f"playbook mentions a path that does not exist: {ref}"

