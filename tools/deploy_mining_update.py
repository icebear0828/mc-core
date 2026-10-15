"""Formal deployment and integrity verification script for Elden Ring Minecraft Mining System update."""
from __future__ import annotations
import hashlib
from pathlib import Path
import shutil
import sys
import time

GAME_DIR = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
MC_CORE_ROOT = Path(__file__).resolve().parent.parent

SRC_DLL = MC_CORE_ROOT / "build" / "bin" / "eldenring" / "dinput8.dll"
SRC_ATLAS = MC_CORE_ROOT / "build" / "authentic_atlas" / "mc_hud_atlas.png"

DST_DLL = GAME_DIR / "dinput8.dll"
DST_ATLAS = GAME_DIR / "mods" / "mc_adapter" / "mc_hud_atlas.png"
BACKUP_DIR = GAME_DIR / "mods" / "mc_adapter" / "backups_mining"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()


def main() -> int:
    print("=" * 70)
    print("ELDEN RING MINING UPDATE DEPLOYMENT & INTEGRITY VERIFICATION")
    print("=" * 70)

    if not GAME_DIR.is_dir():
        print(f"[ERROR] Game directory does not exist: {GAME_DIR}")
        return 1

    if not SRC_DLL.is_file():
        print(f"[ERROR] Source DLL missing: {SRC_DLL}")
        return 1

    if not SRC_ATLAS.is_file():
        print(f"[ERROR] Source authentic atlas missing: {SRC_ATLAS}")
        return 1

    src_dll_hash = sha256_file(SRC_DLL)
    src_dll_size = SRC_DLL.stat().st_size
    src_atlas_hash = sha256_file(SRC_ATLAS)
    src_atlas_size = SRC_ATLAS.stat().st_size

    print(f"Source DLL:   {SRC_DLL} ({src_dll_size:,} B, SHA256: {src_dll_hash})")
    print(f"Source Atlas: {SRC_ATLAS} ({src_atlas_size:,} B, SHA256: {src_atlas_hash})")

    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    ts = int(time.time())

    if DST_DLL.exists():
        backup_dll = BACKUP_DIR / f"dinput8.dll.{ts}.bak"
        shutil.copy2(DST_DLL, backup_dll)
        print(f"Backed up current DLL to: {backup_dll}")

    if DST_ATLAS.exists():
        backup_atlas = BACKUP_DIR / f"mc_hud_atlas.png.{ts}.bak"
        shutil.copy2(DST_ATLAS, backup_atlas)
        print(f"Backed up current atlas to: {backup_atlas}")

    # Deploy
    shutil.copy2(SRC_DLL, DST_DLL)
    shutil.copy2(SRC_ATLAS, DST_ATLAS)

    # Verify destination on disk
    dst_dll_hash = sha256_file(DST_DLL)
    dst_dll_size = DST_DLL.stat().st_size
    dst_atlas_hash = sha256_file(DST_ATLAS)
    dst_atlas_size = DST_ATLAS.stat().st_size

    assert dst_dll_hash == src_dll_hash, "Deployed DLL hash mismatch!"
    assert dst_dll_size == src_dll_size, "Deployed DLL size mismatch!"
    assert dst_atlas_hash == src_atlas_hash, "Deployed atlas hash mismatch!"
    assert dst_atlas_size == src_atlas_size, "Deployed atlas size mismatch!"

    print("-" * 70)
    print("[SUCCESS] All files deployed and verified byte-identical:")
    print(f"  Destination DLL:   {DST_DLL}")
    print(f"    Size:   {dst_dll_size:,} bytes")
    print(f"    SHA256: {dst_dll_hash}")
    print(f"  Destination Atlas: {DST_ATLAS}")
    print(f"    Size:   {dst_atlas_size:,} bytes")
    print(f"    SHA256: {dst_atlas_hash}")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
