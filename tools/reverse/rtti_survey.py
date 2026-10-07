"""Label every object in a pointer tree with its C++ class name (MSVC RTTI), read-only.

  python rtti_survey.py --process game.exe --root-global 0x3d7a1e0 --root-offsets 0x88 --depth 3

Polymorphic objects start with a vtable pointer; the 8 bytes before the vtable point at the RTTI
"complete object locator", which names the class. This turns an anonymous heap of pointers into a map
("ChrModel", "SprjModelDrawEntity", "PlayerCtrl", ...) and is how the model/visibility objects were
found without guessing. Works on any MSVC-built game that kept RTTI.
"""
from __future__ import annotations

import argparse
import re
import struct

from memlib import Proc
from ptrtree import _root, snapshot


def class_name(proc: Proc, image: tuple[int, int], obj: int) -> str | None:
    base, size = image
    inimg = lambda a: a is not None and base <= a < base + size
    vt = proc.u64(obj)
    if not inimg(vt):
        return None
    col = proc.u64(vt - 8)
    if not inimg(col) or proc.u32(col) != 1:  # signature 1 = x64 COL
        return None
    rva = proc.u32(col + 12)
    if rva is None or rva >= size:
        return None
    name = proc.cstring(base + rva + 16, 96)
    if not name or not name.startswith(".?A"):
        return None
    return name.replace(".?AV", "").replace(".?AU", "").replace("@@", "")


def class_hierarchy(proc: Proc, image: tuple[int, int], obj: int, limit: int = 12) -> list[str]:
    """Names of the class and its bases (most derived first)."""
    base, _ = image
    vt = proc.u64(obj)
    col = proc.u64(vt - 8)
    chd = base + proc.u32(col + 16)
    count = proc.u32(chd + 8)
    arr = base + proc.u32(chd + 12)
    names = []
    for i in range(min(count, limit)):
        bcd = base + proc.u32(arr + 4 * i)
        td = base + proc.u32(bcd)
        names.append((proc.cstring(td + 16, 96) or "?").replace(".?AV", "").replace("@@", ""))
    return names


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--process", required=True)
    ap.add_argument("--root-global", type=lambda v: int(v, 0), required=True)
    ap.add_argument("--root-offsets", type=lambda v: [int(x, 0) for x in v.split(",")] if v else [], default=[])
    ap.add_argument("--depth", type=int, default=3)
    ap.add_argument("--show-depth", type=int, default=2, help="always print objects up to this depth")
    ap.add_argument("--filter", default=r"Model|Draw|Alpha|Visib|Render|Mesh|Fade|Opac|Modif|Hide|Appear|Material|Param|Chr",
                    help="regex: deeper objects are printed only when their class matches")
    args = ap.parse_args()

    proc = Proc.attach(args.process)
    image = proc.main_module(args.process)
    root = _root(proc, image, args.root_global, args.root_offsets)
    tree = snapshot(proc, image, root, args.depth)
    key = re.compile(args.filter, re.I)
    rows = []
    for path, (addr, data) in tree.items():
        name = class_name(proc, image, addr)
        if name:
            ones = sum(1 for v in struct.unpack(f"<{len(data)//4}f", data[: len(data)//4*4]) if v == 1.0)
            rows.append((len(path), path, name, ones))
    rows.sort(key=lambda r: (r[0], r[1]))
    print(f"{len(tree)} nodes, {len(rows)} with RTTI")
    seen: set[str] = set()
    for depth, path, name, ones in rows:
        if depth <= args.show_depth or key.search(name):
            tag = " (dup)" if name in seen else ""
            seen.add(name)
            print(f"{depth} {'/'.join(hex(x) for x in path) or '<root>':<34} {name[:70]}  ones={ones}{tag}")


if __name__ == "__main__":
    main()
