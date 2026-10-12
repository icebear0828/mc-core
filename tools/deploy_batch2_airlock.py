#!/usr/bin/env python3
"""
Formal deployment and verification script for Batch 2 (Air Lock Bypass & Fall Timer Reset).
Adheres strictly to Universal Agent System Rules:
- Formal, reproducible script file (no inline one-liners)
- End-to-end file integrity verification (SHA256 matching)
- Game configuration setup (mc_er_steve.txt)
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

    print("=== Batch 2: Air Lock Bypass & Fall Timer Real Deployment ===")
    print(f"Workspace root : {workspace_root}")
    print(f"Target game dir: {game_dir}")

    # 1. Verify build artifact
    src_dll = workspace_root / "build" / "bin" / "eldenring" / "dinput8.dll"
    if not src_dll.is_file():
        print(f"ERROR: Built dll not found: {src_dll}", file=sys.stderr)
        return 1

    src_sha = compute_sha256(src_dll)
    src_size = src_dll.stat().st_size
    print(f"Source DLL size: {src_size} bytes, SHA256: {src_sha}")

    # 2. Backup existing game DLL if present
    dst_dll = game_dir / "dinput8.dll"
    if dst_dll.exists():
        backup_dll = game_dir / "dinput8.dll.old"
        if backup_dll.exists():
            try:
                backup_dll.unlink()
            except Exception:
                pass
        try:
            shutil.copy2(dst_dll, backup_dll)
            print(f"Backed up previous dinput8.dll -> {backup_dll.name}")
        except Exception as e:
            print(f"Warning during backup: {e}")

    # 3. Copy new DLL to game directory
    try:
        shutil.copy2(src_dll, dst_dll)
        print(f"Copied {src_dll.name} -> {dst_dll}")
    except Exception as e:
        print(f"ERROR: Failed to copy to game directory: {e}", file=sys.stderr)
        return 1

    # 4. Verify deployed DLL integrity
    dst_sha = compute_sha256(dst_dll)
    if dst_sha != src_sha:
        print(f"ERROR: SHA256 mismatch after copy! Expected {src_sha}, got {dst_sha}", file=sys.stderr)
        return 1
    print(f"Verified deployed DLL integrity: SHA256 matches ({dst_sha})")

    # 5. Ensure mc_er_steve.txt in game directory has batch 2 settings
    cfg_file = game_dir / "mc_er_steve.txt"
    cfg_lines = []
    if cfg_file.is_file():
        with open(cfg_file, "r", encoding="utf-8", errors="ignore") as f:
            cfg_lines = [line.strip() for line in f if line.strip()]

    # Parse key-values
    cfg_map = {}
    for line in cfg_lines:
        if "=" in line:
            k, v = line.split("=", 1)
            cfg_map[k.strip()] = v.strip()

    # Ensure required batch 1 & 2 flags
    cfg_map["block_drive"] = "1"
    cfg_map["instant_turn"] = "1"
    cfg_map["turn_speed"] = "15.0"
    cfg_map["fall_reset"] = "1"
    cfg_map["fall_hold"] = "1.0"

    with open(cfg_file, "w", encoding="utf-8") as f:
        for k, v in sorted(cfg_map.items()):
            f.write(f"{k}={v}\n")
    print(f"Updated configuration file {cfg_file.name} with:")
    print("  block_drive=1")
    print("  instant_turn=1")
    print("  turn_speed=15.0")
    print("  fall_reset=1")
    print("  fall_hold=1.0")

    print("\n[SUCCESS] Batch 2 deployment fully verified and ready for in-game testing!")
    return 0

if __name__ == "__main__":
    sys.exit(main())
