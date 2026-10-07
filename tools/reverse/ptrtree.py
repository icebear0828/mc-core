"""Pointer-tree snapshots and differential analysis (find positions, camera vectors, ...).

  # 1. snapshot while the character stands still
  python ptrtree.py snap --process game.exe --root-global 0x3d7a1e0 --root-offsets 0x88 --out a.pkl
  # 2. move / rotate in game, snapshot again (same paths are re-resolved)
  python ptrtree.py snap --process game.exe --root-global 0x3d7a1e0 --root-offsets 0x88 --out b.pkl --like a.pkl
  # 3. what changed like a position / like a rotated unit vector?
  python ptrtree.py diff a.pkl b.pkl --mode position
  python ptrtree.py diff a.pkl b.pkl --mode unit-vector

`--root-global` is the RVA of a global holding a pointer (see scan_signature.py); `--root-offsets`
are followed from it ([[global]+o0]+o1...). The tree walk follows every aligned pointer found in each
node up to --depth, never walks into the executable image, and de-duplicates by address.
"""
from __future__ import annotations

import argparse
import math
import pickle
import struct
import zlib
from pathlib import Path

from memlib import Proc

NODE_SIZE = 0x2200
MAX_NODES = 6000


def snapshot(proc: Proc, image: tuple[int, int], root: int, depth: int = 3, node_size: int = NODE_SIZE, max_nodes: int = MAX_NODES):
    """{path(offset chain): (address, bytes)} breadth-first from `root`."""
    base, size = image
    seen: dict[int, tuple] = {}
    out: dict[tuple, tuple[int, bytes]] = {}
    queue = [((), root, 0)]
    while queue and len(out) < max_nodes:
        path, addr, d = queue.pop(0)
        if addr in seen:
            continue
        seen[addr] = path
        data = proc.read(addr, node_size)
        if not data or len(data) < 0x100:
            continue
        out[path] = (addr, data)
        if d >= depth:
            continue
        for off in range(0, len(data) - 7, 8):
            q = struct.unpack_from("<Q", data, off)[0]
            if q < 0x10000 or q > 0x7FFFFFFFFFFF or q & 7 or base <= q < base + size or q in seen:
                continue
            queue.append((path + (off,), q, d + 1))
    return out


def resnapshot(proc: Proc, root: int, paths, node_size: int = NODE_SIZE):
    """Re-read the same offset chains (objects may have moved; pointers are followed again)."""
    out = {}
    for path in paths:
        addr = proc.chain(root, list(path))
        if not addr and path:
            continue
        addr = addr or root
        data = proc.read(addr, node_size)
        if data and len(data) >= 0x100:
            out[path] = (addr, data)
    return out


def save(path: Path, snap) -> None:
    path.write_bytes(zlib.compress(pickle.dumps(snap), 6))


def load(path: Path):
    return pickle.loads(zlib.decompress(path.read_bytes()))


def _floats(data: bytes):
    n = len(data) // 4
    return struct.unpack(f"<{n}f", data[: n * 4])


def find_moving_triples(a, b, min_move: float = 0.5, max_move: float = 80.0, max_abs: float = 5e4):
    """Consecutive float triples that changed between two snapshots by a plausible walking distance.

    Returns [(path, byte_offset, before, after)] sorted shallow-first. A true position shows up many times
    (several copies); a mirrored decoy changes by a different amount.
    """
    hits = []
    for path, (_addr_a, da) in a.items():
        if path not in b:
            continue
        fa, fb = _floats(da), _floats(b[path][1])
        for i in range(min(len(fa), len(fb)) - 2):
            x, y = fa[i : i + 3], fb[i : i + 3]
            if any(not math.isfinite(v) or abs(v) > max_abs for v in x + y):
                continue
            d = [abs(q - p) for p, q in zip(x, y)]
            if sum(1 for v in d if v > min_move) >= 2 and max(d) < max_move and (abs(x[0]) > 0.01 or abs(x[2]) > 0.01):
                hits.append((path, i * 4, x, y))
    hits.sort(key=lambda h: (len(h[0]), h[0], h[1]))
    return hits


def find_rotating_unit_vectors(a, b, min_angle: float = 0.5, tol: float = 2e-3):
    """Float triples of length 1 in both snapshots whose direction changed by > min_angle (camera / facing)."""
    hits = []
    for path, (_addr, da) in a.items():
        if path not in b:
            continue
        fa, fb = _floats(da), _floats(b[path][1])
        for i in range(min(len(fa), len(fb)) - 2):
            x, y = fa[i : i + 3], fb[i : i + 3]
            if any(not math.isfinite(v) for v in x + y):
                continue
            if abs(math.sqrt(sum(v * v for v in x)) - 1) < tol and abs(math.sqrt(sum(v * v for v in y)) - 1) < tol:
                dot = max(-1.0, min(1.0, sum(p * q for p, q in zip(x, y))))
                angle = math.acos(dot)
                if angle > min_angle:
                    hits.append((path, i * 4, x, y, angle))
    hits.sort(key=lambda h: (len(h[0]), h[0], h[1]))
    return hits


def _root(proc: Proc, image: tuple[int, int], global_rva: int, offsets: list[int]) -> int:
    base, _ = image
    first = proc.u64(base + global_rva)
    if not first:
        raise SystemExit("root pointer is null: the object does not exist yet (load into a world first)")
    return proc.chain(first, offsets) if offsets else first


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("snap")
    s.add_argument("--process", required=True)
    s.add_argument("--root-global", type=lambda v: int(v, 0), required=True)
    s.add_argument("--root-offsets", type=lambda v: [int(x, 0) for x in v.split(",")] if v else [], default=[])
    s.add_argument("--depth", type=int, default=3)
    s.add_argument("--out", type=Path, required=True)
    s.add_argument("--like", type=Path, help="reuse the node paths of an earlier snapshot")

    d = sub.add_parser("diff")
    d.add_argument("a", type=Path)
    d.add_argument("b", type=Path)
    d.add_argument("--mode", choices=["position", "unit-vector"], required=True)
    d.add_argument("--limit", type=int, default=40)

    args = ap.parse_args()
    if args.cmd == "snap":
        proc = Proc.attach(args.process)
        image = proc.main_module(args.process)
        root = _root(proc, image, args.root_global, args.root_offsets)
        snap = resnapshot(proc, root, load(args.like).keys()) if args.like else snapshot(proc, image, root, args.depth)
        save(args.out, snap)
        print(f"{len(snap)} nodes saved to {args.out}")
    else:
        a, b = load(args.a), load(args.b)
        if args.mode == "position":
            hits = find_moving_triples(a, b)
            print(f"{len(hits)} candidate triples (look for values that repeat across paths and move at walking speed)")
            for path, off, x, y in hits[: args.limit]:
                print(f"  path {[hex(p) for p in path]} +{off:#x}: ({x[0]:.2f},{x[1]:.2f},{x[2]:.2f}) -> ({y[0]:.2f},{y[1]:.2f},{y[2]:.2f})")
        else:
            hits = find_rotating_unit_vectors(a, b)
            print(f"{len(hits)} unit vectors rotated")
            for path, off, x, y, ang in hits[: args.limit]:
                print(f"  path {[hex(p) for p in path]} +{off:#x}: ({x[0]:.3f},{x[1]:.3f},{x[2]:.3f}) -> ({y[0]:.3f},{y[1]:.3f},{y[2]:.3f})  {ang:.2f} rad")


if __name__ == "__main__":
    main()
