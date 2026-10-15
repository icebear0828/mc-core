#!/usr/bin/env python3
"""
Deploy and verify shield movement penalty update for dinput8.dll to Elden Ring game directory.
Computes SHA256 hashes, creates backup, and verifies deployment integrity.
"""

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

def get_running_game_pids():
    pids = []
    try:
        res = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, check=True)
        for line in res.stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            parts = [p.strip('"') for p in line.split('","')]
            if len(parts) >= 2:
                name = parts[0].lower()
                pid = parts[1]
                if "eldenring" in name or "start_protected_game" in name:
                    pids.append((parts[0], pid))
    except Exception as e:
        print(f"[WARN] Failed to query tasklist: {e}")
    return pids

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
    backup_dll = game_dir / "dinput8.dll.bak_shield_penalty"

    print("=== [Elden Ring Deploy: Shield Movement Penalty & Sprint Cancellation] ===")
    
    # 1. Process check
    running = get_running_game_pids()
    if running:
        print(f"[WARNING] Detected running game processes:")
        for name, pid in running:
            print(f"  - {name} (PID: {pid})")
        print("Note: If file is locked, game must be closed to replace dinput8.dll.")

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
        print(f"Existing game DLL:")
        print(f"  Path:   {dest_dll}")
        print(f"  SHA256: {old_hash}")
        if old_hash == src_hash:
            print("[INFO] Game DLL is already identical to source build.")
            return 0

        # Backup
        try:
            shutil.copy2(dest_dll, backup_dll)
            print(f"  [OK] Backup created -> {backup_dll}")
        except Exception as e:
            print(f"  [WARN] Failed to create backup: {e}")

    # Copy new DLL
    try:
        shutil.copy2(src_dll, dest_dll)
        print(f"  [OK] Successfully copied source DLL to {dest_dll}")
    except PermissionError:
        print(f"[FATAL] Cannot overwrite {dest_dll} because it is currently locked by a running process.")
        print("Please close Elden Ring (start_protected_game.exe) and re-run deployment.")
        return 2
    except Exception as e:
        print(f"[FATAL] Failed to copy DLL: {e}")
        return 1

    # Verify deployed hash
    dest_hash = sha256_file(dest_dll)
    print(f"Deployed DLL SHA256: {dest_hash}")
    if dest_hash != src_hash:
        print("[ERROR] Verification failed: Deployed DLL hash does not match source hash!")
        return 1

    print("=== [PASS] dinput8.dll deployed and verified successfully! ===")
    return 0

if __name__ == "__main__":
    sys.exit(main())
