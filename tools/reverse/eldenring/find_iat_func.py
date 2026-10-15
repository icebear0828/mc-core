#!/usr/bin/env python3
"""
Inspect IAT target at RVA (0x254af16 + 6 + 0x46727c) = 0x29B2198
"""
from pathlib import Path
import struct

exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
target_rva = 0x254af16 + 6 + 0x46727c

print(f"Target IAT RVA: 0x{target_rva:X}")

import pefile
pe = pefile.PE(exe_path)
found = False
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll_name = entry.dll.decode('utf-8')
    for imp in entry.imports:
        if imp.address - pe.OPTIONAL_HEADER.ImageBase == target_rva:
            print(f"Import match: {dll_name} -> {imp.name.decode('utf-8') if imp.name else imp.ordinal}")
            found = True
            break
    if found:
        break
if not found:
    print("Not found in direct IAT search, dumping section .rdata...")
