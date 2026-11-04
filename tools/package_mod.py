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
from typing import Any, Dict, List, Optional, Sequence, Tuple


# The Elden Ring build axes. One package is one exact combination; `all` is the
# combined mc+cs+ow build. The same order is used in the package name and in the
# mc_er_mode.txt the runtime reads on launch.
CANONICAL_MODES: Tuple[str, ...] = ("mc", "cs", "ow")
MODE_ALIASES = {
    "mc": "mc",
    "minecraft": "mc",
    "cs": "cs",
    "cs2": "cs",
    "counter-strike": "cs",
    "ow": "ow",
    "overwatch": "ow",
    "doomfist": "ow",
}


def parse_modes(value: Optional[str]) -> Tuple[str, ...]:
    """Parse ``mc``, ``cs``, ``ow``, ``mc,cs,ow`` or ``all`` into canonical order."""
    if not value:
        return ()
    raw = [part.strip().lower() for part in value.replace("+", ",").split(",") if part.strip()]
    selected = set()
    for part in raw:
        if part == "all":
            selected.update(CANONICAL_MODES)
            continue
        canonical = MODE_ALIASES.get(part)
        if canonical is None:
            raise ValueError(f"unknown mode {part!r}; use mc, cs, ow or all")
        selected.add(canonical)
    return tuple(mode for mode in CANONICAL_MODES if mode in selected)


def mode_key(modes: Sequence[str]) -> str:
    """``('mc', 'cs', 'ow')`` -> ``mc_cs_ow`` (empty -> ``''``)."""
    return "_".join(mode for mode in CANONICAL_MODES if mode in set(modes))


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
        modes: Optional[Sequence[str]] = None,
        start_mode: Optional[str] = None,
        assets_dir: Optional[Path] = None,
    ):
        self.repo_root = repo_root.resolve()
        self.spec = spec
        self.build_dir = build_dir.resolve()
        self.version = version
        self.channel = channel
        self.client_jar = client_jar.resolve() if client_jar else None
        self.assets_dir = assets_dir.resolve() if assets_dir else None
        self.modes = tuple(mode for mode in CANONICAL_MODES if mode in set(modes or ()))
        if self.modes:
            self.start_mode = start_mode or self.modes[0]
            if self.start_mode not in self.modes:
                raise ValueError(f"start mode {self.start_mode!r} is not one of {', '.join(self.modes)}")
        else:
            self.start_mode = None
        # With no explicit modes this is the legacy package: it carries the MC
        # files, exactly as before the mode split.
        self.mc_selected = not self.modes or "mc" in self.modes

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
                "     Download the community asset pack (e.g. mc_adapter_assets_v*.zip)",
                "     and extract the 'mods' folder directly into your game root directory.",
            ])
        else:
            lines.extend([
                " - Full high-res assets and audio are pre-packaged in mods/mc_adapter/.",
            ])

        if self.modes:
            lines.extend([
                "",
                "5. MODE:",
                "------------------------------------------------------------",
                f" - This build contains: {', '.join(self.modes)}",
                f" - Starts in: {self.start_mode} (mc_er_mode.txt; hotkeys still switch every included mode)",
            ])
            mode_notes = self.spec.get("mode_notes", {})
            for mode in self.modes:
                for note in mode_notes.get(mode, []):
                    lines.append(f" - [{mode}] {note}")

        readme_notes = self.spec.get("readme_notes", [])
        if readme_notes:
            lines.extend(["", "6. EXTRA NOTES & CONTROLS:", "------------------------------------------------------------"])
            for note in readme_notes:
                lines.append(f" - {note}")

        lines.append("")
        readme_file.write_text("\n".join(lines), encoding="utf-8")
        return readme_file

    def build_package(self, out_dir: Path, make_zip: bool = True) -> Dict[str, Any]:
        """Assemble the complete package directory and optionally compress it into a zip archive."""
        out_dir = out_dir.resolve()
        out_dir.mkdir(parents=True, exist_ok=True)

        suffix = f"_{mode_key(self.modes)}" if self.modes else ""
        pkg_name = f"mc_{self.spec['game_id']}{suffix}_v{self.version}_{self.channel}"

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

        # 2. Copy required templates / binaries (MC gameplay only)
        if self.mc_selected:
            for req in self.spec.get("required_files", []):
                src_path = self.repo_root / req["src"]
                if not src_path.exists():
                    raise FileNotFoundError(f"Required file not found: {src_path}")
                dest_path = pkg_dir / req["dest"]
                dest_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src_path, dest_path)

        # 3. Create default configuration files (MC gameplay only)
        if self.mc_selected:
            for cfg_name, cfg_content in self.spec.get("default_configs", {}).items():
                cfg_file = pkg_dir / cfg_name
                cfg_file.write_text(cfg_content, encoding="utf-8")

        # 4. Prepare asset directory
        asset_rel_dir = Path(self.spec.get("asset_dir", "mods/mc_adapter"))
        target_asset_dir = pkg_dir / asset_rel_dir
        target_asset_dir.mkdir(parents=True, exist_ok=True)

        # Copy placeholder HUD atlas if exists (MC gameplay only)
        placeholder_atlas = self.repo_root / "assets" / "source" / "textures" / "mc_hud_atlas.png"
        if self.mc_selected and placeholder_atlas.exists():
            shutil.copy2(placeholder_atlas, target_asset_dir / "mc_hud_atlas.png")

        # Per-mode configuration files (e.g. the OW viewmodel tuning file).
        mode_configs = self.spec.get("mode_configs", {})
        for mode in self.modes:
            for cfg_name, cfg_content in mode_configs.get(mode, {}).items():
                (pkg_dir / cfg_name).write_text(cfg_content, encoding="utf-8")

        # Optional private asset bundle (textures, sounds, viewmodels). The bundle
        # mirrors the game root, e.g. mods/mc_adapter/ow_gauntlet.bin, and is copied
        # last so it replaces the EULA-safe placeholders. It is never committed.
        if self.assets_dir is not None:
            if not self.assets_dir.is_dir():
                raise FileNotFoundError(f"Assets directory not found: {self.assets_dir}")
            for src in sorted(self.assets_dir.rglob("*")):
                if not src.is_file():
                    continue
                dest = pkg_dir / src.relative_to(self.assets_dir)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dest)

        # The runtime reads start_mode from mc_er_mode.txt on launch.
        if self.modes:
            mode_file = pkg_dir / "mc_er_mode.txt"
            mode_file.write_text(
                "# Written by tools/package_mod.py: the mode the game starts in.\n"
                f"start_mode={self.start_mode}\n"
                f"available_modes={','.join(self.modes)}\n",
                encoding="utf-8",
            )

        # Channel specific assets
        if self.channel == "full" and self.client_jar and self.client_jar.exists():
            # If client_jar provided in full channel, extract real assets
            pass

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


def package_assets_pack(assets_dir: Path, out_dir: Path, version: str = "0.1.0") -> Dict[str, Any]:
    """Package an extracted mods/mc_adapter folder into a standalone distribution zip."""
    assets_dir = assets_dir.resolve()
    if not assets_dir.exists():
        raise FileNotFoundError(f"Assets directory not found: {assets_dir}")

    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    zip_name = f"mc_adapter_assets_v{version}.zip"
    zip_path = out_dir / zip_name

    checksum_lines = []
    # Collect files
    file_list: List[Path] = []
    for f in sorted(assets_dir.rglob("*")):
        if f.is_file():
            file_list.append(f)
            rel = f.relative_to(assets_dir)
            arcname = f"mods/mc_adapter/{rel.as_posix()}"
            checksum_lines.append(f"{compute_sha256(f)}  {arcname}")

    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for f in file_list:
            rel = f.relative_to(assets_dir)
            arcname = f"mods/mc_adapter/{rel.as_posix()}"
            zf.write(f, arcname=arcname)
        # Write checksum file directly inside the zip
        sums_content = "\n".join(checksum_lines) + "\n"
        zf.writestr("SHA256SUMS.txt", sums_content)

    return {
        "success": True,
        "zip_path": str(zip_path),
        "version": version,
        "files_count": len(file_list),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Package mc-core mod for distribution.")
    parser.add_argument("--game", help="Game identifier (e.g. eldenring, sekiro, wukong)")
    parser.add_argument("--spec", help="Path to package spec JSON (defaults to package_specs/<game>.json)")
    parser.add_argument("--build-dir", type=Path, default=Path("build-win"), help="Build directory containing compiled binaries")
    parser.add_argument("--out-dir", type=Path, default=Path("dist"), help="Output directory for generated packages")
    parser.add_argument("--version", default="0.1.0", help="Version tag (e.g. 0.1.0)")
    parser.add_argument("--channel", choices=["nexus", "full"], default="nexus", help="Distribution channel")
    parser.add_argument("--modes", help="Elden Ring build modes: mc, cs, ow, mc,cs,ow or all")
    parser.add_argument("--start-mode", help="Mode the package starts in (default: the first of --modes)")
    parser.add_argument("--assets-dir", type=Path, help="Private asset bundle mirroring the game root (textures/sounds/viewmodels)")
    parser.add_argument("--client-jar", type=Path, help="Minecraft client.jar path for extracting assets")
    parser.add_argument("--no-zip", action="store_true", help="Do not create zip archive")
    parser.add_argument("--package-assets", action="store_true", help="Package a standalone community assets pack")
    parser.add_argument("--assets-dir", type=Path, help="Path to existing mods/mc_adapter assets folder to package")

    args = parser.parse_args()

    try:
        modes = parse_modes(args.modes)
    except ValueError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        sys.exit(1)
    if args.start_mode and not modes:
        print("[ERROR] --start-mode requires --modes", file=sys.stderr)
        sys.exit(1)
    if args.start_mode and args.start_mode not in modes:
        print(f"[ERROR] start mode {args.start_mode!r} is not one of {', '.join(modes)}", file=sys.stderr)
        sys.exit(1)

    if args.package_assets:
        assets_dir = args.assets_dir or Path("mods/mc_adapter")
        try:
            res = package_assets_pack(assets_dir=assets_dir, out_dir=args.out_dir, version=args.version)
            print(f"[SUCCESS] Standalone assets pack created at: {res['zip_path']} ({res['files_count']} files)")
            return
        except Exception as e:
            print(f"[ERROR] Assets packaging failed: {e}", file=sys.stderr)
            sys.exit(1)

    if not args.game:
        print("[ERROR] --game is required when not using --package-assets", file=sys.stderr)
        sys.exit(1)

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
        modes=modes,
        start_mode=args.start_mode,
        assets_dir=args.assets_dir,
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
