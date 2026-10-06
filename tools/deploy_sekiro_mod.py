"""Deployment and packaging verification script for Sekiro mod adapter."""
from __future__ import annotations
import argparse
import shutil
import sys
from pathlib import Path


def deploy_sekiro_mod(game_dir: Path, build_dir: Path, dry_run: bool = False) -> dict:
    adapter_dll = build_dir / "bin" / "Debug" / "sekiro_adapter.dll"
    if not adapter_dll.exists():
        # Fallback to direct target folder
        adapter_dll = build_dir / "adapters" / "sekiro" / "Debug" / "sekiro_adapter.dll"

    cooked_dir = Path("assets/cooked/sekiro")

    status = {
        "game_dir_exists": game_dir.exists(),
        "adapter_dll_exists": adapter_dll.exists(),
        "adapter_dll_path": str(adapter_dll) if adapter_dll.exists() else None,
        "cooked_assets_exist": cooked_dir.exists(),
        "deployed_files": [],
        "success": False
    }

    if not game_dir.exists():
        print(f"[ERROR] Game directory does not exist: {game_dir}")
        return status

    if not adapter_dll.exists():
        print(f"[ERROR] Adapter DLL not found: {adapter_dll}")
        return status

    mods_dir = game_dir / "mods"
    target_mod_dir = mods_dir / "mc_adapter"

    if not dry_run:
        # Deploy root dinput8.dll proxy hook built strictly by mc-core
        dinput8_src = build_dir / "bin" / "Debug" / "dinput8.dll"
        if not dinput8_src.exists():
            dinput8_src = build_dir / "adapters" / "sekiro" / "Debug" / "dinput8.dll"

        if dinput8_src.exists():
            dest_proxy = game_dir / "dinput8.dll"
            shutil.copy2(dinput8_src, dest_proxy)
            status["deployed_files"].append(str(dest_proxy))
            print(f"[DEPLOY] Successfully deployed mc-core dinput8.dll to {dest_proxy}")
        else:
            print(f"[ERROR] dinput8.dll not found in mc-core build: {dinput8_src}")
            return status

        target_mod_dir.mkdir(parents=True, exist_ok=True)
        dest_dll = target_mod_dir / "sekiro_adapter.dll"
        shutil.copy2(adapter_dll, dest_dll)
        status["deployed_files"].append(str(dest_dll))

        if cooked_dir.exists():
            dest_parts = target_mod_dir / "parts"
            dest_map = target_mod_dir / "map"
            dest_parts.mkdir(parents=True, exist_ok=True)
            dest_map.mkdir(parents=True, exist_ok=True)

            for f in (cooked_dir / "parts").glob("*"):
                shutil.copy2(f, dest_parts / f.name)
                status["deployed_files"].append(str(dest_parts / f.name))

            for f in (cooked_dir / "map").glob("*"):
                shutil.copy2(f, dest_map / f.name)
                status["deployed_files"].append(str(dest_map / f.name))

    status["success"] = True
    return status


def main():
    parser = argparse.ArgumentParser(description="Deploy mc-core Sekiro adapter to game directory.")
    parser.add_argument("--game-dir", type=Path, default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\Sekiro"), help="Sekiro game directory")
    parser.add_argument("--build-dir", type=Path, default=Path("build"), help="mc-core build directory")
    parser.add_argument("--dry-run", action="store_true", help="Inspect without copying files")
    args = parser.parse_args()

    result = deploy_sekiro_mod(args.game_dir, args.build_dir, args.dry_run)
    print("\n--- Sekiro Adapter Deployment Report ---")
    print(f"Game Directory: {args.game_dir}")
    print(f"Game Exists: {result['game_dir_exists']}")
    print(f"Adapter DLL Exists: {result['adapter_dll_exists']}")
    print(f"Cooked Assets Exist: {result['cooked_assets_exist']}")
    print(f"Deployed Files Count: {len(result['deployed_files'])}")
    print(f"Deployment Status: {'SUCCESS' if result['success'] else 'FAILED'}")


if __name__ == "__main__":
    main()
