#!/usr/bin/env python3
"""
inspect_crash_log.py
Extract context lines around the latest crash in mc_er.log.
"""
from pathlib import Path

log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
if not log_path.exists():
    print("Log not found")
    exit(1)

with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
    lines = f.readlines()

last_crash_idx = -1
for i, line in enumerate(lines):
    if "CRASH-VEH:" in line:
        last_crash_idx = i

first_crash_idx = -1
for i in range(5050635, len(lines)):
    if "CRASH-VEH:" in lines[i]:
        first_crash_idx = i
        break

if first_crash_idx != -1:
    start = max(5050635, first_crash_idx - 30)
    end = min(len(lines), first_crash_idx + 10)
    print(f"Context before first crash (lines {start} to {end}):\n")
    for j in range(start, end):
        print(f"{j:7d}: {lines[j]}", end="")
else:
    print("No crash in this session!")
