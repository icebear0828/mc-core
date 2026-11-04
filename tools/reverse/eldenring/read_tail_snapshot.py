#!/usr/bin/env python3
"""
read_tail_snapshot.py
Reads recent lines and snapshot/model log events from mc_er.log efficiently by seeking from EOF.
"""
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_path.is_file():
        print(f"{log_path} not found!")
        return 1

    size = log_path.stat().st_size
    print(f"Log path: {log_path} (Size: {size / (1024*1024):.2f} MB)")

    # Read last 8 MB of logs
    chunk_size = min(size, 8 * 1024 * 1024)
    with open(log_path, "rb") as f:
        f.seek(size - chunk_size)
        data = f.read(chunk_size).decode("utf-8", errors="ignore")

    lines = data.splitlines()
    print(f"Read {len(lines)} lines from tail chunk.")

    keywords = ["snapshot", "snapshot-scan", "hide_native", "hide_mask", "tracked="]
    matched = [l for l in lines if any(k in l.lower() for k in keywords)]
    print(f"\n=== Found {len(matched)} matching lines for snapshot / model hiding ===")
    for l in matched[-100:]:
        print(l)

    print("\n=== Last 20 lines of log ===")
    for l in lines[-20:]:
        print(l)

    return 0

if __name__ == "__main__":
    main()
