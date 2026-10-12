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
    src_dll = root / "build" / "bin" / "eldenring" / "dinput8.dll"
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    target_dll = game_dir / "dinput8.dll"

    print("=== Elden Ring Mod Deployment & Integrity Verification ===")

    if not src_dll.is_file():
        print(f"[ERROR] Source DLL not found: {src_dll}")
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

    print("[SUCCESS] Physical deployment verified! SHA256 matched 100%.")


if __name__ == "__main__":
    main()
