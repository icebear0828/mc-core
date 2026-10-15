#!/usr/bin/env python3
"""
Disassemble instructions preceding RVA 0x254af25 in start_protected_game.exe.
"""
from pathlib import Path
import struct

exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
rva = 0x254af25
file_offset = 0x254A525

with open(exe_path, "rb") as f:
    f.seek(file_offset - 128)
    data = f.read(256)

print("Raw bytes around file_offset:")
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    off = (file_offset - 128) + i
    r = off - 0x600 + 0x1000
    hex_str = " ".join(f"{b:02X}" for b in chunk)
    print(f"0x{r:08X}: {hex_str}")
