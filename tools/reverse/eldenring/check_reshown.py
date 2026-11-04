#!/usr/bin/env python3
"""
check_reshown.py
Searches for native_reshown or reshown occurrences in mc_er.log.
"""
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    if not log_path.is_file():
        return 1

    file_size = log_path.stat().st_size
    chunk_size = 16 * 1024 * 1024
    with open(log_path, "rb") as f:
        f.seek(max(0, file_size - chunk_size))
        chunk = f.read().decode("utf-8", errors="ignore")

    lines = chunk.splitlines()
    matches = [l for l in lines if "reshown" in l.lower() or "hidden" in l.lower() or "hide" in l.lower()]
    print(f"Total matching lines in last 16MB: {len(matches)}")
    for l in matches[-40:]:
        print(l)

    return 0

if __name__ == "__main__":
    main()
