#!/usr/bin/env python3
"""
find_last_snapshot.py
Searches backwards from EOF to find the most recent 'snapshot:' blocks.
"""
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_path.is_file():
        return 1

    file_size = log_path.stat().st_size
    chunk_size = 32 * 1024 * 1024 # 32 MB chunks
    offset = file_size
    snapshots = []

    with open(log_path, "rb") as f:
        while offset > 0 and len(snapshots) < 100:
            read_size = min(chunk_size, offset)
            offset -= read_size
            f.seek(offset)
            chunk = f.read(read_size).decode("utf-8", errors="ignore")
            lines = chunk.splitlines()
            for line in lines:
                if "snapshot" in line.lower() or "f11" in line.lower():
                    snapshots.append(line)
            if snapshots:
                break

    print(f"Scanned back to offset {offset} ({offset/(1024*1024):.1f} MB). Found {len(snapshots)} lines.")
    for s in snapshots[-60:]:
        print(s)

    return 0

if __name__ == "__main__":
    main()
