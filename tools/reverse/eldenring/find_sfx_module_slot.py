#!/usr/bin/env python3
"""
find_sfx_module_slot.py
Find which module slot in ChrIns holds CSChrSfxModule.
"""
from pathlib import Path
import pefile
import capstone
import struct

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

# CSChrSfxModule vtable is at RVA 0x2A3D930
sfx_module_vtable_va = base + 0x2A3D930
print(f"CSChrSfxModule vtable VA: 0x{sfx_module_vtable_va:X}")

# Search for where this vtable pointer is written: "mov qword ptr [rcx], sfx_module_vtable_va"
# 48 c7 01 ... or 48 8d 05 ... mov [rcx], rax
vtable_bytes = struct.pack("<Q", sfx_module_vtable_va)
# In code, often: lea rax, [rip + disp]; mov [rcx], rax
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for sec in pe.sections:
    if sec.Name.rstrip(b'\x00') == b'.text':
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress

# Search for 7-byte lea rax, [rip + disp] targeting sfx_module_vtable_va
for off in range(0, len(text_bytes) - 7):
    # 48 8d 05 xx xx xx xx -> lea rax, [rip + disp]
    # 48 8d 15 xx xx xx xx -> lea rdx, [rip + disp]
    b = text_bytes[off]
    if b == 0x48 and text_bytes[off+1] in (0x8d,):
        reg = text_bytes[off+2]
        if reg in (0x05, 0x0d, 0x15, 0x1d): # rax, rcx, rdx, rbx
            disp = struct.unpack("<i", text_bytes[off+3:off+7])[0]
            curr_va = base + text_rva + off
            target = curr_va + 7 + disp
            if target == sfx_module_vtable_va:
                print(f"Reference to CSChrSfxModule vtable at VA 0x{curr_va:X} (RVA 0x{curr_va - base:X})")
                # Disassemble 30 instructions around it
                snippet = text_bytes[max(0, off - 10) : min(len(text_bytes), off + 80)]
                for ins in cs.disasm(snippet, curr_va - 10):
                    print(f"  0x{ins.address:X}: {ins.mnemonic} {ins.op_str}")
                break
