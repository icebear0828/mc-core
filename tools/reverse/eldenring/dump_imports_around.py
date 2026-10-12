#!/usr/bin/env python3
import pefile

pe = pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
target_va = 0x140000000 + 0x29B2198

for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        if imp.address == target_va or abs(imp.address - target_va) < 64:
            print(f"Diff={imp.address - target_va:3d}: {entry.dll.decode()}!{imp.name.decode() if imp.name else imp.ordinal}")
