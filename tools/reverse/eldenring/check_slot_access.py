#!/usr/bin/env python3
"""
check_slot_access.py
Inspect how ChrIns accesses ChrConditionSfxSeSlot at 0x908 / 0x318.
Is it `mov rax, [rcx + 0x908]` (pointer) or `lea rax, [rcx + 0x908]` (inline struct)?
"""
from pathlib import Path
import pefile
import capstone

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

# Look for ChrConditionSfxSeSlot VTABLE RVA = 0x2A428B0
# Pattern: 64-bit pointer to base + 0x2A428B0
vtable_va = base + 0x2A428B0
import struct
pattern = struct.pack("<Q", vtable_va)

idx = 0
found = []
while True:
    idx = raw_data.find(pattern, idx)
    if idx == -1:
        break
    found.append(idx)
    idx += 8

print(f"Found {len(found)} references to ChrConditionSfxSeSlot vtable:")
for off in found[:10]:
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            rva = sec.VirtualAddress + (off - sec.PointerToRawData)
            print(f"  Offset 0x{off:X} (RVA 0x{rva:X}) in {sec.Name.decode().strip(chr(0))}")

# Also search for instructions with disp 0x908 or 0x318 accessing condition slot
print("\nSearching .text for references to ChrConditionSfxSeSlot constructor/init...")
# ChrConditionSfxSeSlot constructor sets [rcx] = vtable_va
# In x64: lea rax, [rip + disp]; mov [rcx], rax
for sec in pe.sections:
    if sec.Name.decode().strip('\x00') == '.text':
        text_sec = sec
        break

text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
text_rva = text_sec.VirtualAddress

# Search for `lea rax, [rip + disp]` where target is vtable_va
# RVA of vtable is 0x2A428B0
# In 64-bit: 48 8d 05 <disp32>
for off in range(0, len(text_bytes) - 7):
    if text_bytes[off] == 0x48 and text_bytes[off+1] == 0x8D and (text_bytes[off+2] & 0xC7) == 0x05:
        disp = struct.unpack("<i", text_bytes[off+3:off+7])[0]
        ins_rva = text_rva + off
        next_rva = ins_rva + 7
        target_rva = next_rva + disp
        if target_rva == 0x2A428B0:
            print(f"Found lea to ChrConditionSfxSeSlot vtable at RVA 0x{ins_rva:X}:")
            code = text_bytes[off : off + 80]
            for ins in list(cs.disasm(code, base + ins_rva))[:12]:
                print(f"    0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")
