#!/usr/bin/env python3
"""
Deploy and verify batch 3 (Arrow Hit Latency & Feedback Fix) dinput8.dll to Elden Ring game directory.
Computes SHA256 hashes, creates backup, and verifies deployment integrity.
"""

import hashlib
import os
import shutil
import sys
from pathlib import Path

def sha256_file(filepath: Path) -> str:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main() -> int:
    workspace_root = Path(r"D:\code\universal-modder\mc-core")
    src_dll = workspace_root / "build" / "bin" / "eldenring" / "dinput8.dll"
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    dest_dll = game_dir / "dinput8.dll"
    backup_dll = game_dir / "dinput8.dll.bak_batch3"

    print("=== [Batch 3 Deployment & Verification: Arrow Latency Fix] ===")
    if not src_dll.exists():
        print(f"[ERROR] Source DLL not found: {src_dll}")
        return 1

    src_hash = sha256_file(src_dll)
    src_size = src_dll.stat().st_size
    print(f"Source DLL: {src_dll}")
    print(f"  Size:   {src_size:,} bytes")
    print(f"  SHA256: {src_hash}")

    if not game_dir.exists():
        print(f"[ERROR] Target game directory not found: {game_dir}")
        return 1

    if dest_dll.exists():
        old_hash = sha256_file(dest_dll)
        print(f"Existing game DLL SHA256: {old_hash}")
        if not backup_dll.exists():
            shutil.copy2(dest_dll, backup_dll)
            print(f"  Backup created -> {backup_dll}")

    # Copy new DLL
    shutil.copy2(src_dll, dest_dll)
    print(f"Copied to: {dest_dll}")

    # Verify physical file integrity
    dest_hash = sha256_file(dest_dll)
    dest_size = dest_dll.stat().st_size
    print(f"Deployed DLL verification:")
    print(f"  Size:   {dest_size:,} bytes")
    print(f"  SHA256: {dest_hash}")

    if dest_hash != src_hash:
        print("[FATAL ERROR] Hash mismatch between source and deployed DLL!")
        return 2

    print("[SUCCESS] Batch 3 DLL successfully deployed and verified.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
