#!/usr/bin/env python3
"""Find call sites of vtable slot 0x18 or direct calls to 0x140BA95C0."""
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    with exe_path.open("rb") as f:
        raw_data = f.read()

    text_sec = next(s for s in pe.sections if s.Name.decode('latin1').startswith('.text'))
    text_bytes = raw_data[text_sec.PointerToRawData : text_sec.PointerToRawData + text_sec.SizeOfRawData]
    text_rva = text_sec.VirtualAddress

    md = Cs(CS_ARCH_X86, CS_MODE_64)

    # Search for calls in a chunk of text
    # Let's search around CSGrass or render passes
    # Let's search for references to 0x2B7FB00 (the vtable slot itself) or 0x2B7FAE8 (vtable base)
    target_vtable_va = image_base + 0x2B7FAE8
    vt_bytes = target_vtable_va.to_bytes(8, "little")

    # Where in .text is lea reg, [rip + disp] pointing to 0x2B7FAE8?
    print(f"Searching for LEA pointing to vtable 0x{target_vtable_va:X}...")
    target_rva = 0x2B7FAE8
    for off in range(0, len(text_bytes) - 7):
        # 48 8D 05 xx xx xx xx -> lea rax, [rip + rel32]
        if text_bytes[off] == 0x48 and text_bytes[off+1] in (0x8D, 0x8B):
            import struct
            disp = struct.unpack("<i", text_bytes[off+3:off+7])[0]
            curr_rva = text_rva + off
            dest_rva = curr_rva + 7 + disp
            if dest_rva in (target_rva, target_rva - 8, target_rva + 8, 0x2B7FB00):
                print(f"  Found reference at RVA 0x{curr_rva:08X} (VA 0x{image_base + curr_rva:016X})")
                code_chunk = text_bytes[off:off+20]
                for ins in md.disasm(code_chunk, image_base + curr_rva):
                    print(f"    {ins.mnemonic} {ins.op_str}")
                    break

if __name__ == "__main__":
    main()
