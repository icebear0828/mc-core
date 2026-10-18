#!/usr/bin/env python3
"""
analyze_coords_log.py
Extract and analyze actual coordinate events from mc_er.log:
1. Block placement coordinates (blocks: placed ... at (x y z))
2. Virtual summon spawn coordinates (virtual summon created ... at (x, y, z))
3. Player physics coordinates during those events
4. Flight coordinates (flight: pos=(x y z))
5. Coordinate rebase events (flight: coordinates re-based)
6. Camera step events (camstep: ... engine pos=(x y z))
"""

import sys
from pathlib import Path

def main():
    log_file = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_file.exists():
        print(f"[ERROR] Log file not found: {log_file}")
        return 1

    print(f"[ANALYZE] Reading {log_file} ({log_file.stat().st_size:,} bytes)...")

    # Target patterns
    targets = [
        "blocks: placed",
        "virtual summon created",
        "virtual creeper:",
        "re-based",
        "flight: started",
        "flight: stopped",
        "flight: pos=",
        "trace:",
    ]

    matched_lines = {t: [] for t in targets}

    # Stream read from the end of the file (last 50 MB to capture recent sessions)
    file_size = log_file.stat().st_size
    read_size = min(file_size, 50 * 1024 * 1024)
    with open(log_file, "rb") as f:
        if file_size > read_size:
            f.seek(file_size - read_size)
        raw = f.read().decode("utf-8", errors="replace")

    for line in raw.splitlines():
        for t in targets:
            if t in line:
                matched_lines[t].append(line)

    for t in targets:
        lines = matched_lines[t]
        print(f"\n=== Found {len(lines)} occurrences of '{t}' (showing up to last 10) ===")
        for l in lines[-10:]:
            print("  ", l)

    return 0

if __name__ == "__main__":
    sys.exit(main())
