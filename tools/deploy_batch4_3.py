#!/usr/bin/env python3
"""
Formal deployment and verification script for Batch 4.3 (Solid 3D Shield & Arm Fitting).
Follows Universal Agent System Rules:
- No inline scripts; formal, reproducible tooling only.
- Real end-to-end deployment verification with SHA256 integrity check.
"""

from __future__ import annotations
import hashlib
import os
import shutil
import sys
from pathlib import Path

def compute_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main() -> int:
    workspace_root = Path(__file__).resolve().parent.parent
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    mods_dir = game_dir / "mods" / "mc_adapter"

    print("=== Batch 4.3 Real Deployment Verification ===")
    print(f"Workspace root : {workspace_root}")
    print(f"Target game dir: {game_dir}")

    # 1. Verify build artifacts
    src_dll = workspace_root / "build" / "bin" / "eldenring" / "dinput8.dll"
    if not src_dll.is_file():
        print(f"ERROR: Built dll not found: {src_dll}", file=sys.stderr)
        return 1

    src_atlas = workspace_root / "assets" / "source" / "textures" / "mc_hud_atlas.png"
    if not src_atlas.is_file():
        print(f"ERROR: HUD atlas not found: {src_atlas}", file=sys.stderr)
        return 1

    # 2. Deploy target paths
    dst_dll = game_dir / "dinput8.dll"
    dst_atlas = mods_dir / "mc_hud_atlas.png"

    mods_dir.mkdir(parents=True, exist_ok=True)

    # Backup existing dll if present
    if dst_dll.exists():
        backup_dll = game_dir / "dinput8.dll.bak"
        shutil.copy2(dst_dll, backup_dll)
        print(f"[Backup] {backup_dll} saved.")

    # 3. Copy files
    shutil.copy2(src_dll, dst_dll)
    shutil.copy2(src_atlas, dst_atlas)

    # 4. Hash verification
    src_dll_hash = compute_sha256(src_dll)
    dst_dll_hash = compute_sha256(dst_dll)
    src_atlas_hash = compute_sha256(src_atlas)
    dst_atlas_hash = compute_sha256(dst_atlas)

    print("\n--- Integrity Verification ---")
    print(f"Source DLL SHA256     : {src_dll_hash}")
    print(f"Deployed DLL SHA256   : {dst_dll_hash}")
    assert src_dll_hash == dst_dll_hash, "DLL hash mismatch after deployment!"
    print("[PASS] DLL deployed with 100% hash equality.")

    print(f"Source Atlas SHA256   : {src_atlas_hash}")
    print(f"Deployed Atlas SHA256 : {dst_atlas_hash}")
    assert src_atlas_hash == dst_atlas_hash, "Atlas hash mismatch after deployment!"
    print("[PASS] HUD Atlas deployed with 100% hash equality.")

    print("\n=== Deployment Completed Successfully ===")
    return 0

if __name__ == "__main__":
    sys.exit(main())
