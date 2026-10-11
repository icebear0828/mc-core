#!/usr/bin/env python3
"""
Inspect the class vtable and RTTI at RVA 0x00504C89.
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
        
        # At 0x00504C8A: 48 8D 05 EF 9C 54 02 -> lea rax, [rip + 0x02549CEF]
        # Next IP is 0x00504C91
        ins = read_bytes(f, sections, 0x00504C8A, 7)
        disp = struct.unpack("<i", ins[3:7])[0]
        vtable_rva = 0x00504C91 + disp
        vtable_va = IMAGE_BASE + vtable_rva
        print(f"Vtable RVA: 0x{vtable_rva:08X} (VA: 0x{vtable_va:016X})")
        
        # Read Complete Object Locator (COL) at vtable - 8
        col_ptr_bytes = read_bytes(f, sections, vtable_rva - 8, 8)
        col_va = struct.unpack("<Q", col_ptr_bytes)[0]
        col_rva = col_va - IMAGE_BASE
        print(f"COL VA: 0x{col_va:016X} (RVA: 0x{col_rva:08X})")
        
        # COL structure:
        # DWORD signature (0 for x64)
        # DWORD offset
        # DWORD cdOffset
        # DWORD pTypeDescriptor (RVA)
        # DWORD pClassDescriptor (RVA)
        col_bytes = read_bytes(f, sections, col_rva, 24)
        sig, offset, cdOffset, td_rva, chd_rva = struct.unpack("<IIIII", col_bytes[:20])
        print(f"TypeDescriptor RVA: 0x{td_rva:08X}")
        
        # TypeDescriptor:
        # void* pVFTable
        # void* spare
        # char name[]
        td_bytes = read_bytes(f, sections, td_rva, 128)
        raw_name = td_bytes[16:].split(b"\x00")[0].decode("latin-1", errors="ignore")
        print(f"RTTI Type Name: {raw_name}")

if __name__ == "__main__":
    main()
