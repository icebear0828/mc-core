#!/usr/bin/env python3
"""
trace_flver_shadow.py
Analyze GXFlverShadowParamContext and GXCascadedShadowScene.
Locate their vtables and member functions in eldenring.exe.
"""
import pefile
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def find_vtable_for_type_desc(pe, rdata_bytes, rdata_va, td_rva, class_name):
    td_rva_bytes = struct.pack("<I", td_rva)
    pos = rdata_bytes.find(td_rva_bytes)
    if pos == -1:
        print(f"[{class_name}] No CompleteObjectLocator found for TypeDescriptor 0x{td_rva:X}")
        return None
    col_rva = rdata_va + pos - 0xC
    col_va = 0x140000000 + col_rva
    col_va_bytes = struct.pack("<Q", col_va)
    vt_pos = rdata_bytes.find(col_va_bytes)
    if vt_pos == -1:
        print(f"[{class_name}] No vtable found pointing to COL 0x{col_rva:X}")
        return None
    vtable_rva = rdata_va + vt_pos + 8
    print(f"[{class_name}] FOUND VTABLE at RVA 0x{vtable_rva:X}")
    return vtable_rva

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
    rdata_bytes = rdata.get_data()
    rdata_va = rdata.VirtualAddress
    
    classes = [
        (0x03D2DD88, "GXFlverShadowParamContext"),
        (0x03D31A10, "GXCascadedShadowScene"),
        (0x03D33F70, "GXCompartmentShadowScene"),
        (0x03D33FA8, "GXShadowParam"),
        (0x03D33FD8, "GXScreenSpaceShadow"),
        (0x03D372B0, "GXShadowMap"),
    ]
    
    for td_rva, name in classes:
        find_vtable_for_type_desc(pe, rdata_bytes, rdata_va, td_rva, name)

if __name__ == "__main__":
    main()
