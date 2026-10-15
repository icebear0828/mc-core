#!/usr/bin/env python3
"""Find exact function entry and disassemble callers/body."""
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe")
    pe = pefile.PE(str(exe_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    crash_rva = 0xBA965B

    offset = pe.get_offset_from_rva(crash_rva)
    with exe_path.open("rb") as f:
        f.seek(offset - 0x1000)
        raw = f.read(0x1200)

    # Find CC sequences (alignment padding)
    # The last 0xCC sequence before 0x1000
    last_pad = -1
    for i in range(0x1000 - 1, 0, -1):
        if raw[i] == 0xCC and raw[i-1] == 0xCC:
            last_pad = i + 1
            while last_pad < 0x1000 and raw[last_pad] == 0xCC:
                last_pad += 1
            break

    fn_offset = (offset - 0x1000) + last_pad
    fn_rva = crash_rva - (offset - fn_offset)
    print(f"Exact function start RVA: 0x{fn_rva:08X} (VA 0x{image_base + fn_rva:016X})")

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    code = raw[last_pad:]
    for ins in md.disasm(code, image_base + fn_rva):
        rva = ins.address - image_base
        marker = "===> " if (rva == crash_rva) else "     "
        print(f"{marker}0x{ins.address:016X} (RVA 0x{rva:08X}): {ins.mnemonic:8} {ins.op_str}")
        if rva > crash_rva + 0x40:
            break

if __name__ == "__main__":
    main()
