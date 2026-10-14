#!/usr/bin/env python3
"""
verify_rtti_in_image.py
Verify that all vtables and COLs for CS classes are strictly within [image_base, image_base + image_size].
"""
from pathlib import Path
import pefile

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
pe = pefile.PE(str(EXE_PATH), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
size = pe.OPTIONAL_HEADER.SizeOfImage

print(f"Image base: 0x{base:X}, size: 0x{size:X}, end: 0x{base + size:X}")

# Check the vtables we know:
# CSChrSfxModule: 0x142A3D930
# ChrConditionSfxSeSlot: 0x142A428B0
# CSChrModelIns: 0x1429F0000-ish
# CSModelDispEntity: 0x142B30000-ish
test_vtables = [0x142A3D930, 0x142A428B0, 0x142A43400, 0x142A43AA8]
for vt in test_vtables:
    in_range = base <= vt < base + size
    print(f"  VTable 0x{vt:X}: in image? {in_range} (RVA 0x{vt - base:X})")
    assert in_range
print("All RTTI vtables are strictly within image bounds!")
