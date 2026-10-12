#!/usr/bin/env python3
"""
Inspect the functions calling ChrSet constructor (RVA 0x00504C00 - 0x00505000 and 0x00506C00 - 0x00507000)
to determine what class they belong to (e.g. WorldChrManImp) and how ChrSets are laid out.
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
        
        print("=== Code around 0x00504C80 - 0x00504DC0 ===")
        code = read_bytes(f, sections, 0x00504C80, 0x140)
        if code:
            for offset in range(0, len(code), 16):
                chunk = code[offset:offset+16]
                hex_str = " ".join(f"{b:02X}" for b in chunk)
                rva = 0x00504C80 + offset
                print(f"  0x{IMAGE_BASE + rva:016X} (RVA 0x{rva:08X}): {hex_str}")

        print("\n=== Code around 0x00506CE0 - 0x00506D80 ===")
        code2 = read_bytes(f, sections, 0x00506CE0, 0xA0)
        if code2:
            for offset in range(0, len(code2), 16):
                chunk = code2[offset:offset+16]
                hex_str = " ".join(f"{b:02X}" for b in chunk)
                rva = 0x00506CE0 + offset
                print(f"  0x{IMAGE_BASE + rva:016X} (RVA 0x{rva:08X}): {hex_str}")

if __name__ == "__main__":
    main()
