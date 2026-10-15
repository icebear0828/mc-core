#!/usr/bin/env python3
"""
inspect_bullet_bin.py
Examines the bullet template binary in the game folder and in the repo.
"""
from pathlib import Path
import struct

def inspect_bin(path: Path):
    if not path.is_file():
        print(f"File not found: {path}")
        return
    data = path.read_bytes()
    print(f"\n--- {path} ({len(data)} bytes) ---")
    if len(data) >= 0x48:
        owner = struct.unpack_from("<Q", data, 0x00)[0]
        row_id = struct.unpack_from("<I", data, 0x08)[0]
        bullet_param_id = struct.unpack_from("<i", data, 0x14)[0]
        attach_poly = struct.unpack_from("<i", data, 0x1C)[0]
        flags = struct.unpack_from("<I", data, 0x44)[0]
        print(f"  owner: 0x{owner:016X}")
        print(f"  row_id (+0x08): 0x{row_id:08X} ({row_id})")
        print(f"  bullet_param_id (+0x14): {bullet_param_id}")
        print(f"  attach_poly (+0x1C): {attach_poly}")
        print(f"  flags (+0x44): 0x{flags:08X}")
    if len(data) >= 0x90:
        pos = struct.unpack_from("<3f", data, 0x80)
        print(f"  pos (+0x80): ({pos[0]:.2f}, {pos[1]:.2f}, {pos[2]:.2f})")

def main():
    game_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er_bullet.bin")
    tpl_path = Path("tools/reverse/eldenring/templates/mc_er_bullet.bin")
    inspect_bin(game_path)
    inspect_bin(tpl_path)

if __name__ == "__main__":
    main()
