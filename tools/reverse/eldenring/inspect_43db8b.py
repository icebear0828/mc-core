#!/usr/bin/env python3
from pathlib import Path
import pefile
import capstone

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

off = 0x43DB8B - text_rva
snippet = text_bytes[off - 80 : off + 80]
print(f"Function around 0x14043DB8B:")
for ins in cs.disasm(snippet, base + 0x43DB8B - 80):
    print(f"  0x{ins.address:X}: {ins.mnemonic} {ins.op_str}")
