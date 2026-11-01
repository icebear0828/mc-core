"""Lightweight type aliases for the mesh structures the pipeline passes around.

The values stay plain lists/dicts (the OBJ writer and the tests depend on that);
these aliases only document the shapes.
"""
from __future__ import annotations

from typing import TypedDict

Vec2 = tuple[float, float]
Vec3 = tuple[float, float, float]
UV = tuple[float, float, float, float]


class Face(TypedDict):
    verts: list[Vec3]
    uv: UV


class Mesh(TypedDict):
    positions: list[list[float]]
    normals: list[list[float]]
    uv: list[list[float]]
    colors: list[list[float]]
    triangles: list[int]


class Cube(TypedDict):
    origin: Vec3
    size: Vec3
    uv: Vec2
    inflate: float
    mirror: bool


class Bone(TypedDict):
    name: str
    parent: str | None
    pivot: Vec3
    rotation: Vec3
    cubes: list[Cube]


class Geometry(TypedDict):
    identifier: str
    texture_width: int
    texture_height: int
    bones: list[Bone]
