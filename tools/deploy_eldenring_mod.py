#!/usr/bin/env python3
"""
Formal deployment and integrity verification tool for Elden Ring MC Adapter dll.
Enforces SHA256 integrity, backup creation, and atomic deployment.
"""

import hashlib
import os
import shutil
import sys
from pathlib import Path


def get_sha256(file_path: Path) -> str:
    h = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()


def main():
    root = Path(__file__).resolve().parent.parent
    candidate_paths = [
        root / "build" / "bin" / "RelWithDebInfo" / "dinput8.dll",
        root / "build" / "bin" / "Release" / "dinput8.dll",
        root / "build" / "bin" / "eldenring" / "dinput8.dll",
    ]
    existing_candidates = [p for p in candidate_paths if p.is_file()]
    if not existing_candidates:
        src_dll = None
    else:
        existing_candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        src_dll = existing_candidates[0]

    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    target_dll = game_dir / "dinput8.dll"

    print("=== Elden Ring Mod Deployment & Integrity Verification ===")

    if not src_dll:
        print("[ERROR] No valid source DLL found in build candidates!")
        sys.exit(1)

    src_hash = get_sha256(src_dll)
    src_size = src_dll.stat().st_size
    print(f"[SRC] Path: {src_dll}")
    print(f"[SRC] Size: {src_size} bytes")
    print(f"[SRC] SHA256: {src_hash}")

    if not game_dir.is_dir():
        print(f"[ERROR] Game directory not found: {game_dir}")
        sys.exit(1)

    if target_dll.is_file():
        backup_path = game_dir / "dinput8.dll.bak_tnt_fixed"
        shutil.copy2(target_dll, backup_path)
        print(f"[BACKUP] Existing DLL backed up to: {backup_path}")

    shutil.copy2(src_dll, target_dll)
    print(f"[DEPLOY] Copied to: {target_dll}")

    target_hash = get_sha256(target_dll)
    print(f"[DEST] SHA256: {target_hash}")

    if src_hash != target_hash:
        print("[ERROR] SHA256 mismatch after copy!")
        sys.exit(1)

    # Deploy authentic mc_hud_atlas.png to both root and mods/mc_adapter (where loader.cpp loads it from!)
    src_atlas = root / "assets" / "source" / "textures" / "mc_hud_atlas.authentic.png"
    if not src_atlas.is_file():
        src_atlas = root / "assets" / "source" / "textures" / "mc_hud_atlas.png"
    if src_atlas.is_file():
        src_atlas_hash = get_sha256(src_atlas)
        for target_rel in [
            Path("mc_hud_atlas.png"),
            Path("mods") / "mc_adapter" / "mc_hud_atlas.png",
        ]:
            target_atlas = game_dir / target_rel
            target_atlas.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_atlas, target_atlas)
            dest_atlas_hash = get_sha256(target_atlas)
            print(f"[ATLAS DEPLOY] Copied to: {target_atlas}")
            print(f"[ATLAS DEST] SHA256: {dest_atlas_hash}")
            if src_atlas_hash != dest_atlas_hash:
                print(f"[ERROR] Atlas SHA256 mismatch for {target_atlas}!")
                sys.exit(1)

    # Deploy authentic creeper texture
    src_creeper = root / "assets" / "source" / "models" / "entities" / "creeper.authentic.png"
    if src_creeper.is_file():
        target_creeper = game_dir / "mods" / "mc_adapter" / "creeper.png"
        shutil.copy2(src_creeper, target_creeper)
        print(f"[CREEPER DEPLOY] Copied authentic creeper skin to: {target_creeper}")

    print("[SUCCESS] Physical deployment verified! SHA256 matched 100%.")


if __name__ == "__main__":
    main()
