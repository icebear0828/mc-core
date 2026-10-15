#!/usr/bin/env python3
from pathlib import Path

loader_path = Path("adapters/eldenring/src/loader.cpp")
text = loader_path.read_text(encoding="utf-8")
for i, line in enumerate(text.splitlines(), 1):
    if "SetupCreativeProbes" in line:
        print(f"{i}: {line}")
