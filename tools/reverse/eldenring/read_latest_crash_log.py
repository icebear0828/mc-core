#!/usr/bin/env python3
"""Inspect the tail of mc_er.log to analyze crash causes."""
from pathlib import Path
import sys

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_path.exists():
        print(f"Log not found at {log_path}")
        return

    size = log_path.stat().st_size
    print(f"Total log size: {size} bytes")

    # Read the last 64KB
    read_bytes = min(size, 65536)
    with log_path.open("rb") as f:
        f.seek(size - read_bytes)
        data = f.read(read_bytes)

    text = data.decode("utf-8", errors="replace")
    lines = text.splitlines()

    print(f"=== Last {len(lines)} lines of mc_er.log ===")
    for line in lines[-100:]:
        print(line)

if __name__ == "__main__":
    main()
