#!/usr/bin/env python3
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

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for sec in pe.sections:
    if sec.Name.decode().strip('\x00') == '.text':
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress

# Search for calls to 0x4A21E0 or 0x4A22D0
targets = [0x4A21B0, 0x4A21C0, 0x4A21D0, 0x4A21E0, 0x4A22D0, 0x4A22E0]
# call near relative is E8 <disp32>
for off in range(0, len(text_bytes) - 5):
    if text_bytes[off] == 0xE8:
        disp = struct.unpack("<i", text_bytes[off+1:off+5])[0]
        ins_rva = text_rva + off
        next_rva = ins_rva + 5
        target_rva = next_rva + disp
        if target_rva in targets:
            print(f"Call to 0x{target_rva:X} from RVA 0x{ins_rva:X}:")
            # print surrounding code
            start_off = max(0, off - 30)
            code = text_bytes[start_off : off + 20]
            for ins in cs.disasm(code, base + text_rva + start_off):
                print(f"    0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
            print("-" * 50)
