#!/usr/bin/env python3
from pathlib import Path

def main():
    log_path = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_er.log")
    file_size = log_path.stat().st_size
    chunk_size = 16 * 1024 * 1024
    with open(log_path, "rb") as f:
        f.seek(max(0, file_size - chunk_size))
        chunk = f.read().decode("utf-8", errors="ignore")

    lines = chunk.splitlines()
    for l in lines:
        if "steve: hide_native" in l.lower() or "steve-state:" in l.lower():
            print(l)

if __name__ == "__main__":
    main()
