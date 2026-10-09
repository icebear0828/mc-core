#!/usr/bin/env python3
"""Downloads the Minecraft sounds the Elden Ring adapter plays and converts them to 16-bit PCM WAV.

The sounds are not in client.jar: the launcher stores them by hash in its assets folder. This script does what the
launcher does: it reads the version's asset index from Mojang's servers, looks the wanted events up in sounds.json and
fetches those few objects from resources.download.minecraft.net. The output is for local use only (next to the game); it
must never be committed (docs/PORTING_PLAYBOOK.md, "Mojang assets").

    uv run --with soundfile --with numpy python tools/extract_mc_sounds.py --out-dir "<game>/mods/mc_adapter"

Output: <out-dir>/sounds/<event>/<n>.wav and <out-dir>/sounds/sounds_manifest.txt (one line per file:
`event|relative path|volume|pitch_min|pitch_max`).
"""
from __future__ import annotations

import argparse
import io
import json
import random
import sys
import time
import urllib.request
from pathlib import Path

MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
RESOURCES = "https://resources.download.minecraft.net"

# Sound events the adapter plays (see adapters/eldenring/src/audio_xaudio2.cpp and the loader).
EVENTS = [
    # melee
    "entity.player.attack.strong", "entity.player.attack.weak", "entity.player.attack.crit",
    "entity.player.attack.sweep", "entity.player.attack.knockback", "entity.player.attack.nodamage",
    # hurt, death, totem, food
    "entity.player.hurt", "entity.player.death", "item.totem.use", "entity.generic.eat", "entity.player.burp",
    "entity.generic.burn",
    # footsteps (the ground material is not known to the adapter yet: grass and stone are the defaults)
    "block.grass.step", "block.stone.step", "block.gravel.step", "block.sand.step", "block.wood.step",
    "block.grass.fall", "block.stone.fall",
    # bow and arrows
    # experience
    "entity.experience_orb.pickup", "entity.player.levelup",
    "entity.arrow.shoot", "entity.arrow.hit", "entity.arrow.hit_player", "item.crossbow.shoot",
]


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "mc-core-asset-extractor"})
    last: Exception | None = None
    for attempt in range(4):  # the connection is flaky now and then (TLS EOF): retry a few times
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        except Exception as exc:
            last = exc
            time.sleep(1.0 + attempt)
    raise last  # type: ignore[misc]


def fetch_json(url: str):
    return json.loads(fetch(url).decode("utf-8"))


def object_url(h: str) -> str:
    return f"{RESOURCES}/{h[:2]}/{h}"


def to_wav(ogg: bytes) -> bytes:
    import numpy as np
    import soundfile as sf

    data, rate = sf.read(io.BytesIO(ogg), dtype="float32", always_2d=True)
    pcm = (np.clip(data, -1.0, 1.0) * 32767.0).astype("<i2")
    out = io.BytesIO()
    sf.write(out, pcm, rate, format="WAV", subtype="PCM_16")
    return out.getvalue()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="1.21.8")
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--max-variants", type=int, default=6, help="at most this many files per event")
    ap.add_argument("--events", nargs="*", default=EVENTS)
    args = ap.parse_args()

    manifest = fetch_json(MANIFEST_URL)
    entry = next((v for v in manifest["versions"] if v["id"] == args.version), None)
    if entry is None:
        print(f"version {args.version} not found", file=sys.stderr)
        return 1
    version = fetch_json(entry["url"])
    index = fetch_json(version["assetIndex"]["url"])["objects"]
    sounds_json = json.loads(fetch(object_url(index["minecraft/sounds.json"]["hash"])).decode("utf-8"))

    out_root = args.out_dir / "sounds"
    out_root.mkdir(parents=True, exist_ok=True)
    manifest_lines: list[str] = []
    missing: list[str] = []
    rng = random.Random(1)

    for event in args.events:
        spec = sounds_json.get(event)
        if not spec:
            missing.append(event)
            continue
        names: list[tuple[str, float, float, float]] = []  # name, volume, pitch_min, pitch_max
        for s in spec.get("sounds", []):
            if isinstance(s, str):
                names.append((s, 1.0, 1.0, 1.0))
                continue
            if s.get("type") == "event":  # a reference to another event: not followed, the wanted events are listed explicitly
                continue
            pitch = s.get("pitch", 1.0)
            names.append((s["name"], float(s.get("volume", 1.0)), float(pitch), float(pitch)))
        if len(names) > args.max_variants:
            names = rng.sample(names, args.max_variants)
        event_dir = out_root / event
        event_dir.mkdir(parents=True, exist_ok=True)
        count = 0
        for name, volume, pmin, pmax in sorted(names):
            name = name.split(":", 1)[-1]
            key = f"minecraft/sounds/{name}.ogg"
            if key not in index:
                continue
            try:
                wav = to_wav(fetch(object_url(index[key]["hash"])))
            except Exception as exc:  # one broken file must not stop the rest
                print(f"  {event}: {name} failed ({exc})", file=sys.stderr)
                continue
            count += 1
            rel = f"{event}/{count}.wav"
            (out_root / rel).write_bytes(wav)
            manifest_lines.append(f"{event}|{rel}|{volume:.3f}|{pmin:.3f}|{pmax:.3f}")
        print(f"{event}: {count} file(s)")

    (out_root / "sounds_manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    if missing:
        print("events not in sounds.json: " + ", ".join(missing), file=sys.stderr)
    print(f"wrote {len(manifest_lines)} sound files to {out_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
