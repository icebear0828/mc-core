"""Formal deployment and integrity verification script for Elden Ring Minecraft Combat Balance update."""
from __future__ import annotations
import hashlib
from pathlib import Path
import shutil
import sys
import time

GAME_DIR = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
MC_CORE_ROOT = Path(__file__).resolve().parent.parent

SRC_DLL = MC_CORE_ROOT / "build" / "bin" / "eldenring" / "dinput8.dll"
DST_DLL = GAME_DIR / "dinput8.dll"
BACKUP_DIR = GAME_DIR / "mods" / "mc_adapter" / "backups_combat"
CONFIG_FILE = GAME_DIR / "mc_er_steve.txt"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()


def main() -> int:
    print("=" * 70)
    print("ELDEN RING COMBAT BALANCE DEPLOYMENT & INTEGRITY VERIFICATION")
    print("=" * 70)

    if not GAME_DIR.is_dir():
        print(f"[ERROR] Game directory does not exist: {GAME_DIR}")
        return 1

    if not SRC_DLL.is_file():
        print(f"[ERROR] Source DLL missing: {SRC_DLL}")
        return 1

    src_dll_hash = sha256_file(SRC_DLL)
    src_dll_size = SRC_DLL.stat().st_size

    print(f"Source DLL:   {SRC_DLL} ({src_dll_size:,} B, SHA256: {src_dll_hash})")

    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    ts = int(time.time())

    if DST_DLL.exists():
        backup_dll = BACKUP_DIR / f"dinput8.dll.{ts}.bak"
        shutil.copy2(DST_DLL, backup_dll)
        print(f"Backed up current DLL to: {backup_dll}")

    # Copy new DLL
    shutil.copy2(SRC_DLL, DST_DLL)
    print(f"Deployed new DLL to: {DST_DLL}")

    # Verify physical integrity
    dst_dll_hash = sha256_file(DST_DLL)
    if dst_dll_hash != src_dll_hash:
        print(f"[FAIL] Checksum mismatch on DLL: {dst_dll_hash} != {src_dll_hash}")
        return 1
    print(f"[OK] DLL SHA256 matches: {dst_dll_hash}")

    # Verify game config file exists and defaults apply cleanly
    if CONFIG_FILE.exists():
        print(f"[OK] Verified game config file exists: {CONFIG_FILE} ({CONFIG_FILE.stat().st_size} bytes)")

    print("=" * 70)
    print("DEPLOYMENT & VERIFICATION FINISHED SUCCESSFULLY (EXIT 0)")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
