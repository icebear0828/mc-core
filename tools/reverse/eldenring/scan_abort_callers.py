#!/usr/bin/env python3
"""
Scan calls to the abort/__fastfail handler around RVA 0x254af00 in start_protected_game.exe
"""
import struct
import pefile

pe = pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")
text_sec = None
for s in pe.sections:
    if s.Name.startswith(b'.text'):
        text_sec = s
        break

data = text_sec.get_data()
base_va = text_sec.VirtualAddress # RVA 0x1000

target_rva_min = 0x254aee0
target_rva_max = 0x254af30

print(f"Searching references to 0x{target_rva_min:X}..0x{target_rva_max:X}...")
call_count = 0
for i in range(len(data) - 5):
    # E8 rel32 (call) or E9 rel32 (jmp)
    op = data[i]
    if op in (0xE8, 0xE9):
        rel = struct.unpack_from("<i", data, i + 1)[0]
        cur_rva = base_va + i
        dest_rva = cur_rva + 5 + rel
        if target_rva_min <= dest_rva <= target_rva_max:
            call_count += 1
            op_name = "call" if op == 0xE8 else "jmp"
            print(f"Found {op_name} at RVA 0x{cur_rva:X} -> 0x{dest_rva:X}")
            if call_count >= 30:
                print("Too many calls, truncated...")
                break
print(f"Total calls found: {call_count}")
