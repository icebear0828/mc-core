#!/usr/bin/env python3
"""Unified mod packaging and distribution tool for mc-core adapters."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Optional


def load_spec(spec_path: Path) -> Dict[str, Any]:
    """Load and parse game adapter packaging specification JSON."""
    if not spec_path.exists():
        raise FileNotFoundError(f"Specification file not found: {spec_path}")
    with spec_path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    return data


def compute_sha256(file_path: Path) -> str:
    """Compute SHA256 hex digest for a file."""
    h = hashlib.sha256()
    with file_path.open("rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


class ModPackager:
    def __init__(
        self,
        repo_root: Path,
        spec: Dict[str, Any],
        build_dir: Path,
        version: str = "0.1.0",
        channel: str = "nexus",
        client_jar: Optional[Path] = None,
    ):
        self.repo_root = repo_root.resolve()
        self.spec = spec
        self.build_dir = build_dir.resolve()
        self.version = version
        self.channel = channel
        self.client_jar = client_jar.resolve() if client_jar else None

    def _generate_readme(self, dest_dir: Path) -> Path:
        readme_file = dest_dir / "README_INSTALL.txt"
        lines = [
            f"============================================================",
            f" {self.spec.get('display_name', 'mc-core Mod')} - v{self.version}",
            f" Channel: {self.channel.upper()}",
            f"============================================================",
            "",
            "1. INSTALLATION (STANDALONE):",
            "------------------------------------------------------------",
            f" - Place all files in this archive into your game directory",
            f"   (the folder containing '{self.spec.get('target_exe', 'game executable')}').",
            "",
            "2. ANTI-CHEAT & OFFLINE REQUIREMENT:",
            "------------------------------------------------------------",
            " - IMPORTANT: Run the game strictly OFFLINE with Anti-Cheat (EAC) disabled.",
            " - Do NOT go online with modded dinput8.dll.",
            "",
            "3. MOD ENGINE 2 (ME2) USERS:",
            "------------------------------------------------------------",
            f" - If you already use Mod Engine 2, rename '{self.spec.get('dll_name', 'dinput8.dll')}'",
            f"   to '{self.spec.get('me2_dll_name', 'mc_adapter.dll')}'.",
            f" - Add it to your ME2 config TOML:",
            f'     external_dlls = ["{self.spec.get("me2_dll_name", "mc_adapter.dll")}"]',
            "",
            "4. ASSETS & SOUNDS SETUP:",
            "------------------------------------------------------------",
        ]

        if self.channel == "nexus":
            lines.extend([
                " - This package includes placeholder HUD sprites and models to comply with Mojang EULA.",
                " - To use authentic high-res Minecraft UI, Steve skin and 90+ game sounds:",
                "     1) Copy your own Minecraft client.jar (1.21.x) to this folder or pass its path.",
                "     2) Double-click 'setup_assets.bat' and follow instructions.",
            ])
        else:
            lines.extend([
                " - Full high-res assets and audio are pre-packaged in mods/mc_adapter/.",
            ])

        readme_notes = self.spec.get("readme_notes", [])
        if readme_notes:
            lines.extend(["", "5. EXTRA NOTES & CONTROLS:", "------------------------------------------------------------"])
            for note in readme_notes:
                lines.append(f" - {note}")

        lines.append("")
        readme_file.write_text("\n".join(lines), encoding="utf-8")
        return readme_file

    def _generate_setup_assets_bat(self, dest_dir: Path) -> Path:
        bat_file = dest_dir / "setup_assets.bat"
        content = (
            "@echo off\r\n"
            "echo ============================================================\r\n"
            "echo  Extract Minecraft Assets for mc-core Adapter\r\n"
            "echo ============================================================\r\n"
            "echo.\r\n"
            "set /p CLIENT_JAR=\"Please enter the path to your Minecraft client.jar (e.g. client_1.21.jar): \"\r\n"
            "if not exist \"%CLIENT_JAR%\" (\r\n"
            "    echo [ERROR] File not found: %CLIENT_JAR%\r\n"
            "    pause\r\n"
            "    exit /b 1\r\n"
            ")\r\n"
            "echo Extracting assets...\r\n"
            "python tools\\extract_mc_assets.py --client-jar \"%CLIENT_JAR%\" --export-hud-atlas --export-steve-skin --out-dir \"mods\\mc_adapter\"\r\n"
            "python tools\\extract_mc_sounds.py --out-dir \"mods\\mc_adapter\\sounds\"\r\n"
            "echo.\r\n"
            "echo [DONE] Assets extracted to mods\\mc_adapter.\r\n"
            "pause\r\n"
        )
        bat_file.write_text(content, encoding="utf-8")
        return bat_file

    def build_package(self, out_dir: Path, make_zip: bool = True) -> Dict[str, Any]:
        """Assemble the complete package directory and optionally compress it into a zip archive."""
        out_dir = out_dir.resolve()
        out_dir.mkdir(parents=True, exist_ok=True)

        pkg_name = f"mc_{self.spec['game_id']}_v{self.version}_{self.channel}"
        pkg_dir = out_dir / pkg_name
        if pkg_dir.exists():
            shutil.rmtree(pkg_dir)
        pkg_dir.mkdir(parents=True, exist_ok=True)

        # 1. Copy main DLL
        dll_name = self.spec.get("dll_name", "dinput8.dll")
        dll_src = self.build_dir / dll_name
        if not dll_src.exists():
            # Check nested paths
            nested_candidates = [
                self.build_dir / "bin" / self.spec["game_id"] / dll_name,
                self.build_dir / "bin" / "Release" / dll_name,
                self.build_dir / "Release" / dll_name,
            ]
            for cand in nested_candidates:
                if cand.exists():
                    dll_src = cand
                    break

        if not dll_src.exists():
            raise FileNotFoundError(f"Target DLL '{dll_name}' not found in build directory: {self.build_dir}")

        shutil.copy2(dll_src, pkg_dir / dll_name)

        # 2. Copy required templates / binaries
        for req in self.spec.get("required_files", []):
            src_path = self.repo_root / req["src"]
            if not src_path.exists():
                raise FileNotFoundError(f"Required file not found: {src_path}")
            dest_path = pkg_dir / req["dest"]
            dest_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_path, dest_path)

        # 3. Create default configuration files
        for cfg_name, cfg_content in self.spec.get("default_configs", {}).items():
            cfg_file = pkg_dir / cfg_name
            cfg_file.write_text(cfg_content, encoding="utf-8")

        # 4. Prepare asset directory
        asset_rel_dir = Path(self.spec.get("asset_dir", "mods/mc_adapter"))
        target_asset_dir = pkg_dir / asset_rel_dir
        target_asset_dir.mkdir(parents=True, exist_ok=True)

        # Copy placeholder HUD atlas if exists
        placeholder_atlas = self.repo_root / "assets" / "source" / "textures" / "mc_hud_atlas.png"
        if placeholder_atlas.exists():
            shutil.copy2(placeholder_atlas, target_asset_dir / "mc_hud_atlas.png")

        # Channel specific assets
        if self.channel == "nexus":
            self._generate_setup_assets_bat(pkg_dir)
        elif self.channel == "full" and self.client_jar and self.client_jar.exists():
            # If client_jar provided in full channel, extract real assets
            pass  # Future direct asset extraction integration

        # 5. Generate README
        self._generate_readme(pkg_dir)

        # 6. Generate SHA256SUMS.txt
        checksum_lines = []
        for f in sorted(pkg_dir.rglob("*")):
            if f.is_file() and f.name != "SHA256SUMS.txt":
                rel = f.relative_to(pkg_dir)
                checksum_lines.append(f"{compute_sha256(f)}  {rel.as_posix()}")

        sums_file = pkg_dir / "SHA256SUMS.txt"
        sums_file.write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")

        zip_path_str = None
        if make_zip:
            zip_path = out_dir / f"{pkg_name}.zip"
            with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
                for f in sorted(pkg_dir.rglob("*")):
                    if f.is_file():
                        zf.write(f, arcname=f.relative_to(pkg_dir).as_posix())
            zip_path_str = str(zip_path)

        return {
            "success": True,
            "package_dir": str(pkg_dir),
            "zip_path": zip_path_str,
            "game_id": self.spec["game_id"],
            "version": self.version,
            "channel": self.channel,
        }


def main() -> None:
    parser = argparse.ArgumentParser(description="Package mc-core mod for distribution.")
    parser.add_argument("--game", required=True, help="Game identifier (e.g. eldenring, sekiro, wukong)")
    parser.add_argument("--spec", help="Path to package spec JSON (defaults to package_specs/<game>.json)")
    parser.add_argument("--build-dir", type=Path, default=Path("build-win"), help="Build directory containing compiled binaries")
    parser.add_argument("--out-dir", type=Path, default=Path("dist"), help="Output directory for generated packages")
    parser.add_argument("--version", default="0.1.0", help="Version tag (e.g. 0.1.0)")
    parser.add_argument("--channel", choices=["nexus", "full"], default="nexus", help="Distribution channel")
    parser.add_argument("--client-jar", type=Path, help="Minecraft client.jar path for extracting assets")
    parser.add_argument("--no-zip", action="store_true", help="Do not create zip archive")

    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    spec_path = Path(args.spec) if args.spec else repo_root / "package_specs" / f"{args.game}.json"

    if not spec_path.exists():
        print(f"[ERROR] Spec file not found: {spec_path}", file=sys.stderr)
        sys.exit(1)

    spec = load_spec(spec_path)
    packager = ModPackager(
        repo_root=repo_root,
        spec=spec,
        build_dir=args.build_dir,
        version=args.version,
        channel=args.channel,
        client_jar=args.client_jar,
    )

    try:
        res = packager.build_package(out_dir=args.out_dir, make_zip=not args.no_zip)
        print(f"[SUCCESS] Package generated at: {res['package_dir']}")
        if res.get("zip_path"):
            print(f"[SUCCESS] ZIP archive created at: {res['zip_path']}")
    except Exception as e:
        print(f"[ERROR] Packaging failed: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
