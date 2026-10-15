#!/usr/bin/env python3
"""
inspect_wolf_vfx.py
Inspect SpEffectParam row 20297130 and find what vfxId and stateInfo it uses.
Also inspect NpcParam 140700000 and BuddyParam 23200000.
"""
from pathlib import Path
import xml.etree.ElementTree as ET

# Look for param defs
param_dir = Path(r"D:\code\Seep-Reverse-Lab\tools\windows\param_cache")

def find_in_file(filename, search_str):
    path = param_dir / filename
    if not path.is_file():
        return
    print(f"--- Search in {filename} for '{search_str}' ---")
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if search_str in line:
                print(line.strip())

find_in_file("BuddyParam.txt", "23200000")
find_in_file("NpcParam.txt", "140700000")
find_in_file("SpEffectParam.txt", "20297130")
