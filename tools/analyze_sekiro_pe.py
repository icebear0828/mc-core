"""Analyze Sekiro PE binary sections and search for FromSoftware signatures."""
import pefile
from pathlib import Path


def main():
    exe_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Sekiro\sekiro.exe")
    if not exe_path.exists():
        print(f"Exe not found: {exe_path}")
        return

    pe = pefile.PE(str(exe_path), fast_load=True)
    print(f"ImageBase: 0x{pe.OPTIONAL_HEADER.ImageBase:X}")
    print(f"NumberOfSections: {len(pe.sections)}")
    for s in pe.sections:
        name = s.Name.decode('utf-8', errors='ignore').strip('\x00')
        print(f"  Section: {name:8s} VirtualAddress: 0x{s.VirtualAddress:08X} Size: 0x{s.SizeOfRawData:08X}")


if __name__ == "__main__":
    main()
