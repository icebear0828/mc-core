"""Find a character's real facing field from snapshots taken while it stands, while only the camera turns,
and after it walked in known directions (see ptrtree.py for taking snapshots).

  python facing.py search --still a.pkl --camera b.pkl --walk c.pkl:dx,dz --walk d.pkl:dx,dz

`dx,dz` is the horizontal direction the character walked before stopping (the difference of two position
readings): characters face where they walked. A facing field
  * does not change when only the camera turns (camera.pkl vs still.pkl), and
  * after each walk equals that walk's heading under ONE fixed convention (sign and zero direction), either
    as an angle in radians or as a pair of floats holding the horizontal direction vector.
Requiring two walks with different headings rejects constants and coincidences.
"""
from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

TAU = 2 * math.pi
ANGLE_OFFSETS = (0.0, math.pi / 2, -math.pi / 2, math.pi)
VECTOR_MODELS = [(swap, sa, sb) for swap in (False, True) for sa in (1, -1) for sb in (1, -1)]


def heading(dx: float, dz: float) -> float:
    """Angle of the walk direction, 0 along +Z, growing toward +X."""
    return math.atan2(dx, dz)


def _wrap(a: float) -> float:
    return (a + math.pi) % TAU - math.pi


def _floats(data: bytes) -> tuple[float, ...]:
    n = len(data) // 4
    return struct.unpack(f"<{n}f", data[: n * 4])


def _stable(fa: tuple[float, ...], fb: tuple[float, ...], i: int, width: int, eps: float) -> bool:
    return all(math.isfinite(fa[i + k]) and abs(fa[i + k] - fb[i + k]) < eps for k in range(width))


def _angle_models(value: float, head: float):
    """Yield (sign, offset) pairs for which value == sign*head + offset (mod 2pi) within tolerance."""
    for sign in (1, -1):
        for off in ANGLE_OFFSETS:
            yield sign, off, abs(_wrap(value - (sign * head + off)))


def _vector_models(a: float, b: float, head: float):
    dx, dz = math.sin(head), math.cos(head)
    for swap, sa, sb in VECTOR_MODELS:
        ex, ez = (dz, dx) if swap else (dx, dz)
        yield (swap, sa, sb), math.hypot(a - sa * ex, b - sb * ez)


def find_facing_fields(still, camera, walks, angle_tol: float = 0.15, vec_tol: float = 0.12, stable_eps: float = 1e-4):
    """still/camera: snapshots {path: (addr, bytes)}; walks: [(snapshot, (dx, dz))].

    Returns a list of dicts: {kind: 'angle'|'vector', path, offset, model}. Needs >= 2 walks whose headings
    differ by more than 0.5 rad.
    """
    heads = [heading(dx, dz) for _, (dx, dz) in walks]
    if len(walks) < 2 or max(abs(_wrap(h - heads[0])) for h in heads) < 0.5:
        raise ValueError("need at least two walks with clearly different headings")
    hits = []
    for path, (_addr, data_still) in still.items():
        if path not in camera or any(path not in snap for snap, _ in walks):
            continue
        fs, fc = _floats(data_still), _floats(camera[path][1])
        fw = [_floats(snap[path][1]) for snap, _ in walks]
        n = min(len(fs), len(fc), *(len(f) for f in fw))
        for i in range(n):
            if _stable(fs, fc, i, 1, stable_eps) and math.isfinite(fw[0][i]) and abs(fw[0][i]) <= 2 * TAU:
                common = None
                for f, h in zip(fw, heads):
                    ok = {(s, o) for s, o, err in _angle_models(f[i], h) if err < angle_tol}
                    common = ok if common is None else common & ok
                    if not common:
                        break
                for model in sorted(common or ()):
                    hits.append({"kind": "angle", "path": path, "offset": i * 4, "model": model})
        for width_gap in (1, 2):
            for i in range(n - width_gap):
                if not (_stable(fs, fc, i, 1, stable_eps) and _stable(fs, fc, i + width_gap, 1, stable_eps)):
                    continue
                common = None
                for f, h in zip(fw, heads):
                    a, b = f[i], f[i + width_gap]
                    if not (math.isfinite(a) and math.isfinite(b)) or abs(a) > 1.5 or abs(b) > 1.5:
                        common = set()
                        break
                    ok = {m for m, err in _vector_models(a, b, h) if err < vec_tol}
                    common = ok if common is None else common & ok
                    if not common:
                        break
                for model in sorted(common or ()):
                    hits.append({"kind": "vector", "path": path, "offset": i * 4, "gap": width_gap, "model": model})
    hits.sort(key=lambda h: (len(h["path"]), h["path"], h["offset"]))
    return hits


def _parse_walk(text: str):
    file, vec = text.rsplit(":", 1)
    dx, dz = (float(v) for v in vec.split(","))
    return Path(file), (dx, dz)


def main() -> None:
    from ptrtree import load

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("search")
    s.add_argument("--still", type=Path, required=True)
    s.add_argument("--camera", type=Path, required=True)
    s.add_argument("--walk", action="append", required=True, help="snapshot.pkl:dx,dz")
    s.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()
    walks = [(load(p), d) for p, d in map(_parse_walk, args.walk)]
    hits = find_facing_fields(load(args.still), load(args.camera), walks)
    print(f"{len(hits)} candidate fields")
    for h in hits[: args.limit]:
        where = f"path {[hex(p) for p in h['path']]} +{h['offset']:#x}"
        extra = f" gap {h['gap']}" if "gap" in h else ""
        print(f"  {h['kind']:6} {where}{extra} model {h['model']}")


if __name__ == "__main__":
    main()
