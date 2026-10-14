#!/usr/bin/env python3
"""
find_sfx_module_registration.py
Find where CSChrSfxModule is registered on ChrIns.
"""
from pathlib import Path
import pefile
import capstone
import struct

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

for sec in pe.sections:
    if sec.Name.rstrip(b'\x00') == b'.text':
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Find calls to 0x14047A9A0 or 0x14047A9B0 or around 0x47AA00
target_rvas = [0x47AA10, 0x47A9A0, 0x47A9D0, 0x47AA00]
print("Looking for callers of CSChrSfxModule ctor/methods...")
for off in range(0, len(text_bytes) - 5):
    if text_bytes[off] == 0xE8:
        rel = struct.unpack("<i", text_bytes[off+1:off+5])[0]
        cur_rva = text_rva + off
        dest_rva = cur_rva + 5 + rel
        if 0x47A980 <= dest_rva <= 0x47AA50:
            print(f"Call to 0x{dest_rva:X} from RVA 0x{cur_rva:X}")
            snippet = text_bytes[max(0, off - 30) : min(len(text_bytes), off + 40)]
            for ins in cs.disasm(snippet, base + cur_rva - 30):
                print(f"    0x{ins.address:X}: {ins.mnemonic} {ins.op_str}")
            print()
