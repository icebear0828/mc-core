#!/usr/bin/env python3
"""
Inspect the faulting RVA 0x254af25 inside start_protected_game.exe.
"""
from pathlib import Path
import struct

exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
rva = 0x254af25

print(f"Inspecting EXE: {exe_path}")
with open(exe_path, "rb") as f:
    dos = f.read(64)
    e_lfanew = struct.unpack_from("<I", dos, 0x3C)[0]
    f.seek(e_lfanew)
    pe_sig = f.read(4)
    coff = f.read(20)
    num_sections = struct.unpack_from("<H", coff, 2)[0]
    opt_header_size = struct.unpack_from("<H", coff, 16)[0]
    opt = f.read(opt_header_size)
    image_base = struct.unpack_from("<Q", opt, 24)[0]
    
    sections = []
    for _ in range(num_sections):
        sec = f.read(40)
        name = sec[:8].rstrip(b'\x00').decode('ascii', errors='ignore')
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", sec, 8)
        sections.append((name, va, vsize, rptr, rsize))
        print(f"Section {name}: VA=0x{va:X}, VSize=0x{vsize:X}, RawOffset=0x{rptr:X}")

target_sec = None
file_offset = None
for name, va, vsize, rptr, rsize in sections:
    if va <= rva < va + vsize:
        target_sec = name
        file_offset = rptr + (rva - va)
        print(f"\nRVA 0x{rva:X} falls into section [{name}] at file offset 0x{file_offset:X}")
        break

if file_offset is not None:
    with open(exe_path, "rb") as f:
        # Read +/- 64 bytes
        start = max(0, file_offset - 32)
        f.seek(start)
        data = f.read(96)
        print("\nHex dump around fault offset:")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            curr_addr = start + i
            curr_rva = (curr_addr - rptr) + va
            hex_str = " ".join(f"{b:02X}" for b in chunk)
            marker = " <== FAULT" if curr_addr <= file_offset < curr_addr + 16 else ""
            print(f"File 0x{curr_addr:08X} (RVA 0x{curr_rva:08X}): {hex_str:<48}{marker}")
