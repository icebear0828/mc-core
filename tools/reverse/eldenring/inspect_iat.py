#!/usr/bin/env python3
import pefile

pe = pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
print(f"ImageBase = 0x{pe.OPTIONAL_HEADER.ImageBase:X}")
target_rva = 0x254af16 + 6 + 0x46727c

for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        imp_rva = imp.address - pe.OPTIONAL_HEADER.ImageBase
        if abs(imp_rva - target_rva) < 64:
            print(f"RVA Diff={imp_rva - target_rva:3d}: {entry.dll.decode()}!{imp.name.decode() if imp.name else imp.ordinal}")
