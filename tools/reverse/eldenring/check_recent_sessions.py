#!/usr/bin/env python3
"""
check_recent_sessions.py
Finds recent startup markers, session initializations, and keypress events in mc_er.log.
"""
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_path.is_file():
        print("Log not found")
        return 1

    size = log_path.stat().st_size
    print(f"Log size: {size / (1024*1024):.2f} MB")

    chunk_size = min(size, 4 * 1024 * 1024)
    with open(log_path, "rb") as f:
        f.seek(size - chunk_size)
        data = f.read(chunk_size).decode("utf-8", errors="ignore")

    lines = data.splitlines()
    print(f"Read {len(lines)} lines from tail.")

    # Look for banner or initialization lines
    init_lines = [l for l in lines if "dinput8" in l.lower() or "steve:" in l.lower() or "elden ring" in l.lower()]
    print(f"\n--- Found {len(init_lines)} init/steve lines in tail ---")
    for l in init_lines[-30:]:
        print(l)

    # Look for F11 or snapshot
    f11_lines = [l for l in lines if "f11" in l.lower() or "snapshot" in l.lower()]
    print(f"\n--- Found {len(f11_lines)} F11/snapshot lines in tail ---")
    for l in f11_lines[-50:]:
        print(l)

    return 0

if __name__ == "__main__":
    main()
