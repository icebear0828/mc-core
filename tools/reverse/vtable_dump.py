"""Dump an object's class hierarchy and the short virtual functions of its vtable (needs capstone).

  uv run --with capstone python vtable_dump.py --process game.exe --root-global 0x3d7a1e0 \
      --root-offsets 0x88,0x48,0x250

Tiny virtual functions are the cheapest documentation a binary has: `mov byte ptr [rcx+0x826], dl ; ret`
is a bool setter at offset 0x826, `mov dword ptr [rcx+0x70], edx ; ret` an int setter, and so on. Look
for setters of the property you want to change (visibility, alpha, scale), then read the *current* value
on the live object to see which offsets are plausible before writing anything.

Caveats learned the hard way: functions shared between two classes (same address) belong to a common base
class and use the same offset in both; a function's `this` may be a sub-object (a thunk with `add rcx,N`
adjusts it), so confirm the value you read looks like the type the setter writes.
"""
from __future__ import annotations

import argparse

from memlib import Proc
from ptrtree import _root
from rtti_survey import class_hierarchy


def main() -> None:
    try:
        from capstone import CS_ARCH_X86, CS_MODE_64, Cs
    except ImportError:
        raise SystemExit("capstone is required: uv run --with capstone python vtable_dump.py ...")

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--process", required=True)
    ap.add_argument("--root-global", type=lambda v: int(v, 0), required=True)
    ap.add_argument("--root-offsets", type=lambda v: [int(x, 0) for x in v.split(",")] if v else [], default=[])
    ap.add_argument("--max", type=int, default=70, help="how many vtable slots to list")
    args = ap.parse_args()

    proc = Proc.attach(args.process)
    image = proc.main_module(args.process)
    base, size = image
    obj = _root(proc, image, args.root_global, args.root_offsets)
    vt = proc.u64(obj)
    print(f"object {obj:#x}, vtable RVA {vt - base:#x}")
    print("bases:", " <- ".join(class_hierarchy(proc, image, obj)))

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    for i in range(args.max):
        fp = proc.u64(vt + 8 * i)
        if fp is None or not (base <= fp < base + size):
            break
        code = proc.read(fp, 48)
        ins = list(md.disasm(code or b"", fp))
        short = next((ins[: k + 1] for k, x in enumerate(ins[:9]) if x.mnemonic == "ret"), None)
        text = " ; ".join(f"{x.mnemonic} {x.op_str}" for x in short) if short else "(long)"
        print(f"  [{i:2d}] {fp - base:#9x}  {text}")


if __name__ == "__main__":
    main()
