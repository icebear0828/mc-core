#!/usr/bin/env python3
from pathlib import Path
import pefile
import struct
import capstone

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def offset_to_rva(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

td_pos = 0x3D03BD8 - 0x10
td_rva = offset_to_rva(td_pos)
col_pattern = struct.pack("<III", 1, 0, 0) + struct.pack("<I", td_rva)
col_off = raw_data.find(col_pattern)
col_rva = offset_to_rva(col_off)
col_va = base + col_rva
vt_pattern = struct.pack("<Q", col_va)
vt_off = raw_data.find(vt_pattern)
vt_rva = offset_to_rva(vt_off + 8)
print(f"SoloParamRepository vtable RVA: 0x{vt_rva:X}")

# Search for lea rcx, [vtable] or mov [singleton], rax
for sec in pe.sections:
    if sec.Name.startswith(b'.text'):
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for off in range(0, len(text_bytes) - 7):
    if text_bytes[off] == 0x48 and text_bytes[off+1] == 0x8D and (text_bytes[off+2] & 0xC7) == 0x05:
        disp = struct.unpack("<i", text_bytes[off+3:off+7])[0]
        ins_rva = text_rva + off
        next_rva = ins_rva + 7
        if next_rva + disp == vt_rva:
            print(f"Found constructor at RVA 0x{ins_rva:X}:")
            code = text_bytes[off : off + 50]
            for ins in cs.disasm(code, base + ins_rva):
                print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
