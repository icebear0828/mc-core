#!/usr/bin/env python3
"""
Inspect the entire function containing RVA 0x254af25 in start_protected_game.exe.
"""
from pathlib import Path
import struct

exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
rva = 0x254af25
file_offset = 0x254A525

with open(exe_path, "rb") as f:
    # Read function beginning around 0x254aee0 to 0x254af60
    f.seek(file_offset - 80)
    data = f.read(160)

# Print as hex and try simple x64 disassembly
print(f"Bytes from RVA 0x{rva - 80:X} to 0x{rva + 80:X}:")
try:
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    base_addr = 0x140000000 + (rva - 80)
    for ins in md.disasm(data, base_addr):
        marker = " <=== CRASH" if ins.address == 0x140000000 + rva else ""
        print(f"0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}{marker}")
except ImportError:
    print("capstone not installed, showing raw hex:")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_str = " ".join(f"{b:02X}" for b in chunk)
        print(f"{i:04X}: {hex_str}")
