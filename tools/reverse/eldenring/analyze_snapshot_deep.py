#!/usr/bin/env python3
"""
analyze_snapshot_deep.py
Analyzes all snapshot sessions and F11 dumps from mc_er.log.
"""
from pathlib import Path
import re

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_path.is_file():
        print(f"Log not found at {log_path}")
        return 1

    size = log_path.stat().st_size
    print(f"Log size: {size / (1024*1024):.2f} MB")

    # Read last 32 MB of logs to capture all recent runs
    chunk_size = min(size, 32 * 1024 * 1024)
    with open(log_path, "rb") as f:
        f.seek(size - chunk_size)
        data = f.read(chunk_size).decode("utf-8", errors="ignore")

    lines = data.splitlines()
    print(f"Read {len(lines)} lines from tail.")

    # Find all sessions or F11 snapshots
    snapshot_lines = []
    for line in lines:
        if "snapshot" in line.lower() or "f11" in line.lower() or "hide_native" in line.lower():
            snapshot_lines.append(line)

    print(f"\nTotal snapshot-related lines found: {len(snapshot_lines)}")
    for l in snapshot_lines[-120:]:
        print(l)

    # Search for all scan hits (player+ or model+)
    scan_hits = [l for l in lines if "snapshot-scan:" in l]
    print(f"\nTotal snapshot-scan hits: {len(scan_hits)}")
    for l in scan_hits[-50:]:
        print(l)

    return 0

if __name__ == "__main__":
    main()
