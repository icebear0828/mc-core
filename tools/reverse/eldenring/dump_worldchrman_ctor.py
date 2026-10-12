#!/usr/bin/env python3
"""
Disassemble and analyze WorldChrManImp constructor (0x00504C00 - 0x00505500)
to find all ChrSets, pointers, and arrays.
"""

import struct

EXE_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe"
IMAGE_BASE = 0x140000000

def read_pe(f):
    f.seek(0x3C)
    e_lfanew = struct.unpack("<I", f.read(4))[0]
    f.seek(e_lfanew + 0x14)
    size_of_opt = struct.unpack("<H", f.read(2))[0]
    opt_header_offset = e_lfanew + 0x18
    section_headers_offset = opt_header_offset + size_of_opt
    f.seek(e_lfanew + 0x6)
    num_sections = struct.unpack("<H", f.read(2))[0]
    sections = []
    f.seek(section_headers_offset)
    for _ in range(num_sections):
        sec_data = f.read(40)
        name = sec_data[:8].rstrip(b"\x00").decode("latin-1", errors="ignore")
        vsize, vrva, raw_size, raw_offset = struct.unpack("<IIII", sec_data[8:24])
        sections.append({
            "name": name,
            "vsize": vsize,
            "vrva": vrva,
            "raw_size": raw_size,
            "raw_offset": raw_offset,
        })
    return sections

def rva_to_offset(sections, rva):
    for sec in sections:
        if sec["vrva"] <= rva < sec["vrva"] + max(sec["vsize"], sec["raw_size"]):
            return sec["raw_offset"] + (rva - sec["vrva"])
    return None

def read_bytes(f, sections, rva, length):
    off = rva_to_offset(sections, rva)
    if off is None:
        return None
    f.seek(off)
    return f.read(length)

def main():
    with open(EXE_PATH, "rb") as f:
        sections = read_pe(f)
        
        # Read WorldChrManImp constructor from 0x00504C70 to 0x00505500
        start_rva = 0x00504C70
        length = 0x900
        code = read_bytes(f, sections, start_rva, length)
        
        # Search for all offsets written to rbx / rcx / rdi (this pointer)
        # e.g., mov [rbx + disp32], ... or lea rcx, [rbx + disp32]
        # In x64:
        # 48 89 xx [disp32]
        # 48 8D xx [disp32]
        # C7 xx [disp32] ...
        print("=== Disassembly dump of WorldChrManImp constructor ===")
        # Let's inspect instructions by dumping chunks of 32 bytes with hex
        for i in range(0, length, 32):
            chunk = code[i:i+32]
            hex_str = " ".join(f"{b:02X}" for b in chunk[:16])
            hex_str2 = " ".join(f"{b:02X}" for b in chunk[16:32])
            rva = start_rva + i
            print(f"0x{rva:08X}: {hex_str}")
            print(f"            {hex_str2}")

if __name__ == "__main__":
    main()
