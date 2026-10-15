#!/usr/bin/env python3
"""
read_bow_log.py
Reads recent lines and bow/bullet events from mc_er.log efficiently by seeking from EOF.
"""
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_path.is_file():
        print(f"{log_path} not found!")
        return

    size = log_path.stat().st_size
    print(f"Log path: {log_path}")
    print(f"Log size: {size} bytes ({size / (1024*1024):.2f} MB)")

    # Read last 2 MB
    chunk_size = min(size, 2 * 1024 * 1024)
    with open(log_path, "rb") as f:
        f.seek(size - chunk_size)
        data = f.read(chunk_size).decode("utf-8", errors="ignore")

    lines = data.splitlines()
    print(f"Read {len(lines)} lines from tail chunk.")

    print("\n=== All bullet requests recorded or spawned ===")
    matches = [l for l in lines if "bullet #" in l or "bullet template:" in l or "bullet fire:" in l]
    print(f"Total matching bullet lines: {len(matches)}")
    for m in matches[:30]:
        print(m)

    print("\n=== Last 25 lines of mc_er.log ===")
    for l in lines[-25:]:
        print(l)

if __name__ == "__main__":
    main()
