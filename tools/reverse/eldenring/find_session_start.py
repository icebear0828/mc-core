#!/usr/bin/env python3
from pathlib import Path

log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
lines = log_path.read_text(encoding="utf-8", errors="ignore").splitlines()

# Search backwards for session start
start_idx = 0
for i in range(len(lines) - 1, -1, -1):
    if "environment check ok" in lines[i] or "=== init ===" in lines[i] or "[   0.000]" in lines[i]:
        start_idx = i
        break

print(f"Session started at line {start_idx} (total {len(lines)}):")
for l in lines[start_idx : start_idx + 80]:
    print(l)
