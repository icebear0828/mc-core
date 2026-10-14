#!/usr/bin/env python3
from pathlib import Path
import pefile

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

for sec in pe.sections:
    if sec.Name.startswith(b'.data'):
        data_sec = sec
        break

data_bytes = raw_data[data_sec.PointerToRawData : data_sec.PointerToRawData + data_sec.SizeOfRawData]
pos = 0
found = []
while True:
    idx = data_bytes.find(b".?AVSoloParamRepository", pos)
    if idx == -1: break
    end = data_bytes.find(b"\x00", idx)
    name = data_bytes[idx:end].decode("latin-1")
    rva = data_sec.VirtualAddress + idx
    print(f"RVA 0x{rva:X}: {name}")
    pos = idx + 4
