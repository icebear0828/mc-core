#!/usr/bin/env python3
"""
inspect_summon_sfx.py - Analyze ChrConditionSfxSeSlot and SpecialEffect vtables and RTTI in eldenring.exe
"""
from pathlib import Path
import struct
import pefile

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
if not EXE_PATH.exists():
    EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\start_protected_game.exe")

print(f"Loading {EXE_PATH}...")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
rdata_sec = None
data_sec = None
text_sec = None

for sec in pe.sections:
    name = sec.Name.decode('ascii', errors='ignore').strip('\x00')
    if name == '.rdata':
        rdata_sec = sec
    elif name == '.data':
        data_sec = sec
    elif name == '.text':
        text_sec = sec

with open(EXE_PATH, "rb") as f:
    raw_data = f.read()

def rva_to_offset(rva):
    for sec in pe.sections:
        if sec.VirtualAddress <= rva < sec.VirtualAddress + sec.Misc_VirtualSize:
            return sec.PointerToRawData + (rva - sec.VirtualAddress)
    return None

def offset_to_rva(off):
    for sec in pe.sections:
        if sec.PointerToRawData <= off < sec.PointerToRawData + sec.SizeOfRawData:
            return sec.VirtualAddress + (off - sec.PointerToRawData)
    return None

def find_rtti_type_descriptor(typename):
    pattern = b".?AV" + typename.encode('ascii') + b"@"
    idx = 0
    results = []
    while True:
        idx = raw_data.find(pattern, idx)
        if idx == -1:
            break
        # type descriptor starts 0x10 bytes before the mangled string
        td_offset = idx - 0x10
        td_rva = offset_to_rva(td_offset)
        if td_rva:
            results.append((td_offset, td_rva))
        idx += len(pattern)
    return results

def find_complete_object_locators(td_rva):
    # in x64 RTTI, COL has signature=1, offset=0, cd_offset=0, type_descriptor_rva, class_hierarchy_descriptor_rva
    # byte pattern: 01 00 00 00 00 00 00 00 00 00 00 00 <td_rva:u32>
    pattern = struct.pack("<III", 1, 0, 0) + struct.pack("<I", td_rva)
    idx = 0
    results = []
    while True:
        idx = raw_data.find(pattern, idx)
        if idx == -1:
            break
        col_rva = offset_to_rva(idx)
        if col_rva:
            results.append((idx, col_rva))
        idx += len(pattern)
    return results

def find_vtables_for_col(col_rva):
    # The vtable has a pointer to COL at (vtable - 8)
    col_va = base + col_rva
    pattern = struct.pack("<Q", col_va)
    idx = 0
    results = []
    while True:
        idx = raw_data.find(pattern, idx)
        if idx == -1:
            break
        vtable_off = idx + 8
        vtable_rva = offset_to_rva(vtable_off)
        if vtable_rva:
            results.append((vtable_off, vtable_rva))
        idx += 8
    return results

classes_to_check = [
    "ChrConditionSfxSeSlot",
    "SpecialEffect",
    "NpcSpEffectEquipCtrl",
    "CSChrModelIns",
    "CSModelDispEntity",
    "CSFD4ModelItem"
]

for cls in classes_to_check:
    print(f"\n=== Searching RTTI for: {cls} ===")
    tds = find_rtti_type_descriptor(cls)
    print(f"Found {len(tds)} TypeDescriptors")
    for td_off, td_rva in tds:
        print(f"  TD at RVA 0x{td_rva:X} (VA 0x{base + td_rva:X})")
        cols = find_complete_object_locators(td_rva)
        print(f"  Found {len(cols)} COLs")
        for col_off, col_rva in cols:
            print(f"    COL at RVA 0x{col_rva:X} (VA 0x{base + col_rva:X})")
            vtables = find_vtables_for_col(col_rva)
            print(f"    Found {len(vtables)} vtables")
import capstone
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

print("\n=== Disassembling ChrConditionSfxSeSlot methods ===")
for rva in [0x4A2330, 0x498890, 0x4A2370, 0x498850, 0x4A2480, 0x4A2390, 0x4A23A0]:
    off = rva_to_offset(rva)
    if off:
        code = raw_data[off : off + 48]
        print(f"\nMethod at RVA 0x{rva:X} (VA 0x{base + rva:X}):")
        for ins in list(cs.disasm(code, base + rva))[:8]:
            print(f"  0x{ins.address:X}: {ins.mnemonic:<8} {ins.op_str}")

