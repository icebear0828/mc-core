"""Find a byte signature in the *running* (decrypted) game image and show what it points at.

  python scan_signature.py --process game.exe --aob "48 8B 35 ?? ?? ?? ?? 44 0F 28 18"

Packed/encrypted executables show nothing on disk: the code only exists in memory once the game has
started, so scan the live process. A good signature matches exactly once. For every match this prints the
RIP-relative global it loads and the pointer currently stored there (0 = the object does not exist yet,
e.g. on the title screen).
"""
from __future__ import annotations

import argparse

from memlib import Proc, aob, rip_target


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--process", required=True, help="image name, e.g. sekiro.exe")
    ap.add_argument("--aob", required=True, help='signature with ?? wildcards')
    ap.add_argument("--disp-offset", type=int, default=3, help="offset of the rel32 inside the instruction (default 3)")
    ap.add_argument("--instr-len", type=int, default=7, help="length of the instruction (default 7)")
    ap.add_argument("--max", type=int, default=20)
    args = ap.parse_args()

    proc = Proc.attach(args.process)
    base, size = proc.main_module(args.process)
    image, bad = proc.read_image(base, size)
    print(f"image base {base:#x} size {size:#x} unreadable {bad:#x}")
    print(f"first code bytes: {image[0x1000:0x1010].hex(' ')}  (random-looking = still encrypted, wait and retry)")

    matches = [m.start() for m in aob(args.aob).finditer(image)]
    print(f"{len(matches)} match(es)")
    for off in matches[: args.max]:
        glob = rip_target(image, off, args.disp_offset, args.instr_len)
        value = proc.u64(base + glob)
        print(f"  match RVA {off:#x} -> global RVA {glob:#x} holds {value:#x}" if value is not None else f"  match RVA {off:#x} -> global RVA {glob:#x} (unreadable)")
    if len(matches) != 1:
        print("NOTE: a usable signature should match exactly once; validate several candidates structurally instead of guessing.")


if __name__ == "__main__":
    main()
