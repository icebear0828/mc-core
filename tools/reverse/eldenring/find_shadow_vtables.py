#!/usr/bin/env python3
import pefile
import struct
from pathlib import Path

EXE_PATH = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")

def main():
    pe = pefile.PE(str(EXE_PATH), fast_load=True)
    rdata = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
    rdata_bytes = rdata.get_data()
    rdata_rva = rdata.VirtualAddress
    
    # In search_chr_vtables.py:
    # target_td_rva = string_rva - 0x10!
    # Because TypeDescriptor begins 0x10 bytes before the mangled string name!
    classes = [
        (0x03D2DD88 - 0x10, "GXFlverShadowParamContext"),
        (0x03D31A10 - 0x10, "GXCascadedShadowScene"),
        (0x03D33F70 - 0x10, "GXCompartmentShadowScene"),
        (0x03D33FA8 - 0x10, "GXShadowParam"),
        (0x03D33FD8 - 0x10, "GXScreenSpaceShadow"),
        (0x03D372B0 - 0x10, "GXShadowMap"),
        (0x03D2EB60 - 0x10, "GXUpdateJob<GXShadowParamManager>"),
        (0x03D2F348 - 0x10, "GXFlverModelRenderer"),
        (0x03D37BA0 - 0x10, "GXRenderingPipeline"),
    ]
    
    td_dict = {td: name for td, name in classes}
    print("Searching for CompleteObjectLocators in .rdata...")
    for i in range(0, len(rdata_bytes) - 24, 4):
        sig, offset, cdOffset, td_rva = struct.unpack("<IIII", rdata_bytes[i:i+16])
        if sig == 1 and td_rva in td_dict:
            col_rva = rdata_rva + i
            col_va = 0x140000000 + col_rva
            name = td_dict[td_rva]
            print(f"FOUND COL for {name} at RVA 0x{col_rva:X} (offset {offset})")
            
            # find vtable pointing to this COL
            col_va_bytes = struct.pack("<Q", col_va)
            idx = rdata_bytes.find(col_va_bytes)
            if idx != -1:
                vt_rva = rdata_rva + idx + 8
                print(f"  ==> VTABLE at RVA 0x{vt_rva:X}")

if __name__ == "__main__":
    main()
