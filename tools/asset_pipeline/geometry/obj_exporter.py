"""Wavefront OBJ serialisation."""
from __future__ import annotations


def export_obj(name: str, mesh: dict) -> str:
    lines = [f"# Model: {name}", f"o {name}"]

    for x, y, z in mesh["positions"]:
        lines.append(f"v {x:.4f} {y:.4f} {z:.4f}")

    for u, v in mesh["uv"]:
        lines.append(f"vt {u:.4f} {v:.4f}")

    for nx, ny, nz in mesh["normals"]:
        lines.append(f"vn {nx:.4f} {ny:.4f} {nz:.4f}")

    # OBJ indices are 1-based (v/vt/vn)
    triangles = mesh["triangles"]
    for i in range(0, len(triangles), 3):
        i0, i1, i2 = triangles[i] + 1, triangles[i + 1] + 1, triangles[i + 2] + 1
        lines.append(f"f {i0}/{i0}/{i0} {i1}/{i1}/{i1} {i2}/{i2}/{i2}")

    return "\n".join(lines) + "\n"
