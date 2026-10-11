#!/usr/bin/env python3
"""
Disassemble WorldChrManImp methods and find all ChrSet references and character lists.
"""

import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

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
        
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        
        # Disassemble WorldChrManImp constructor from RVA 0x00504C80 to 0x00505400
        start_rva = 0x00504C80
        length = 0x800
        code = read_bytes(f, sections, start_rva, length)
        
        print("=== Disassembly of WorldChrManImp constructor ===")
        for ins in md.disasm(code, IMAGE_BASE + start_rva):
            # Print instructions that access memory or call functions
            op_str = ins.op_str
            print(f"0x{ins.address:016X} (RVA 0x{ins.address - IMAGE_BASE:08X}): {ins.mnemonic:8s} {op_str}")

if __name__ == "__main__":
    main()
