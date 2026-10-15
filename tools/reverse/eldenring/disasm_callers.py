#!/usr/bin/env python3
"""
Disassemble caller sites to RVA 0x254AEF0.
"""
import pefile
import capstone

pe = pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

callers = [
    0x1E53C46, 0x1E6B5A0, 0x24FC3DF, 0x251ED34, 0x25219BB, 0x2522FAA,
    0x2554BAE, 0x2554C55, 0x25585A5, 0x255FA2A, 0x25601BE, 0x2562C1F
]

for c in callers:
    data = pe.get_data(c - 20, 40)
    base = 0x140000000 + (c - 20)
    print(f"\n--- Caller at RVA 0x{c:X} ---")
    for ins in md.disasm(data, base):
        marker = " <=== CALL" if ins.address == 0x140000000 + c else ""
        print(f"0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}{marker}")
