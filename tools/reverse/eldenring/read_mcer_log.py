#!/usr/bin/env python3
"""
read_mcer_log.py
Read the latest lines from mc_er.log in the game folder.
"""
from pathlib import Path

log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
if not log_path.is_file():
    print(f"{log_path} not found!")
else:
    print(f"Log size: {log_path.stat().st_size} bytes")
    lines = log_path.read_text(encoding="utf-8", errors="ignore").splitlines()
    print(f"Total lines: {len(lines)}")
    print("\n--- Last 50 lines ---")
    for l in lines[-50:]:
        print(l)
