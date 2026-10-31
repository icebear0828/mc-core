#!/usr/bin/env python3
"""Decode the Overwatch Doomfist mod's Unreal Bink Audio into 44.1 kHz 16-bit WAV.

The mod ships `MIX_*.ubulk` files: a `SEEK` chunk with 16-bit block sizes followed
by Bink Audio 2 blocks (`0x9999` magic + 16-bit frame size). The companion
`.uasset` carries a `BinkAudioFileHeader` (`UEBA` tag, stored as `ABEU` on disk)
with the sample rate, channel count and total sample count.

This script rebuilds a standard UEBA `.binka` container and hands it to FFmpeg,
which needs UEBA support (`avformat/binka` gained it in September 2025). The
decoded WAVs are written under `<out-dir>/sounds/ow/` and registered in
`sounds_manifest.txt` as `ow.*` events. Nothing is committed: the audio stays in
the user's game folder.

    uv run python tools/extract_ow_sounds.py \
        --audio-dir "D:/code/universal-modder/ow_assets/pbz_unpack/Mods/WuchangDoomfist/Original/Audio" \
        --out-dir "<game>/mods/mc_adapter" \
        --ffmpeg "C:/path/to/ffmpeg.exe"
"""
from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

# The two events the OW mode plays today. Add more here if the state machine
# grows a charge/hit sound.
EVENTS = {
    "MIX_原版手炮完整开火": "ow.cannon.fire",
    "MIX_原版直拳释放": "ow.punch.release",
}

BLOCK_MAGIC = 0x9999
SEEK_MAGIC = b"SEEK"
UEBA_TAG = b"ABEU"  # 'UEBA' in little-endian byte order
FRAME_SAMPLES = 1920  # 48 kHz Bink Audio 2 block (2048-point RDFT minus 128 overlap)


@dataclass
class AudioInfo:
    channels: int
    sample_rate: int
    total_samples: int


@dataclass
class Block:
    payload: bytes
    trim_samples: int | None = None  # set for a trimmed final block


def read_audio_info(uasset: Path) -> AudioInfo:
    """Reads the embedded BinkAudioFileHeader from a UE5 .uasset."""
    data = uasset.read_bytes()
    at = data.find(UEBA_TAG)
    if at < 0:
        raise ValueError(f"{uasset.name}: no {UEBA_TAG!r} header")
    if at + 28 > len(data):
        raise ValueError(f"{uasset.name}: truncated header")
    (_tag, version, channels, _padding, rate, sample_count,
     _max_comp, _flags, _output_size, _seek_count, _blocks_per_entry) = struct.unpack_from(
        "<4sBBHIIHHIHH", data, at)
    if version != 1 or channels < 1:
        raise ValueError(f"{uasset.name}: unsupported header (version={version}, channels={channels})")
    return AudioInfo(channels=channels, sample_rate=rate, total_samples=sample_count)


def parse_ubulk(ubulk: Path) -> list[Block]:
    """Parses the SEEK chunk and the Bink Audio blocks that follow it."""
    data = ubulk.read_bytes()
    if len(data) < 16 or data[:4] != SEEK_MAGIC:
        raise ValueError(f"{ubulk.name}: no SEEK chunk")
    block_count = struct.unpack_from(">I", data, 8)[0]
    if block_count <= 0:
        raise ValueError(f"{ubulk.name}: empty block table")

    start = next((i for i in range(len(data) - 1) if data[i] == 0x99 and data[i + 1] == 0x99), -1)
    if start < 0:
        raise ValueError(f"{ubulk.name}: no 0x9999 block header")

    blocks: list[Block] = []
    pos = start
    while pos + 4 <= len(data):
        frame_check = struct.unpack_from("<I", data, pos)[0]
        if (frame_check & 0xFFFF) != BLOCK_MAGIC:
            raise ValueError(f"{ubulk.name}: bad block magic at {pos:#x}")
        frame_size = frame_check >> 16
        header = 4
        trim = None
        if frame_size == 0xFFFF:
            if pos + 8 > len(data):
                raise ValueError(f"{ubulk.name}: truncated trimmed-block header")
            trimmed = struct.unpack_from("<I", data, pos + 4)[0]
            frame_size = trimmed & 0xFFFF
            trim = trimmed >> 16
            header = 8
        end = pos + header + frame_size
        if end > len(data):
            raise ValueError(f"{ubulk.name}: block at {pos:#x} runs past EOF")
        blocks.append(Block(payload=data[pos + header:end], trim_samples=trim))
        pos = end

    if pos != len(data):
        raise ValueError(f"{ubulk.name}: {len(data) - pos} trailing bytes after the last block")
    if len(blocks) != block_count:
        raise ValueError(f"{ubulk.name}: header says {block_count} blocks, parsed {len(blocks)}")
    return blocks


def build_binka(info: AudioInfo, blocks: list[Block]) -> bytes:
    """Wraps the blocks in the UEBA container the FFmpeg binka demuxer reads."""
    out = bytearray()
    out += UEBA_TAG
    out += bytes([1, info.channels])
    out += struct.pack("<H", 0)  # PADDING
    out += struct.pack("<I", info.sample_rate)
    out += struct.pack("<I", info.total_samples)
    out += struct.pack("<H", max(len(b.payload) for b in blocks))
    out += struct.pack("<H", 1)  # flags: Bink Audio 2
    out += struct.pack("<I", sum(len(b.payload) + 6 for b in blocks))
    out += struct.pack("<H", len(blocks))
    out += struct.pack("<H", 1)  # blocks_per_seek_table_entry
    for block in blocks:
        out += struct.pack("<H", len(block.payload))
    for block in blocks:
        out += b"\x99\x99"
        if block.trim_samples is not None:
            out += struct.pack("<HHH", 0xFFFF, len(block.payload), block.trim_samples)
        else:
            out += struct.pack("<H", len(block.payload))
        out += block.payload
    return bytes(out)


def decode(ffmpeg: str, binka: bytes, out_wav: Path) -> None:
    out_wav.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ow_binka_") as tmp:
        src = Path(tmp) / "audio.binka"
        src.write_bytes(binka)
        cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "binka", "-i", str(src),
               "-c:a", "pcm_s16le", "-ar", "44100", "-ac", "1", str(out_wav)]
        result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg failed: {result.stderr.strip() or result.stdout.strip()}")


def update_manifest(manifest: Path, entries: list[tuple[str, str]]) -> None:
    """Replaces the `ow.` lines, keeping every other event untouched."""
    kept: list[str] = []
    if manifest.exists():
        for line in manifest.read_text(encoding="utf-8").splitlines():
            if line.strip() and not line.split("|", 1)[0].startswith("ow."):
                kept.append(line)
    for event, rel_path in entries:
        kept.append(f"{event}|{rel_path}|1.0|1.0|1.0")
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text("\n".join(kept) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--audio-dir", type=Path, required=True,
                    help="the unpacked .../Original/Audio folder (uasset + ubulk pairs)")
    ap.add_argument("--out-dir", type=Path, required=True,
                    help="the game's mods/mc_adapter folder; WAVs go to sounds/ow/")
    ap.add_argument("--ffmpeg", default=os.environ.get("FFMPEG", "ffmpeg"),
                    help="FFmpeg with UEBA binka support (default: $FFMPEG or ffmpeg on PATH)")
    ap.add_argument("--manifest", type=Path, default=None,
                    help="default: <out-dir>/sounds/sounds_manifest.txt")
    args = ap.parse_args(argv)

    manifest = args.manifest or (args.out_dir / "sounds" / "sounds_manifest.txt")
    sounds_dir = args.out_dir / "sounds" / "ow"
    entries: list[tuple[str, str]] = []
    failures = 0

    for stem, event in EVENTS.items():
        uasset = args.audio_dir / f"{stem}.uasset"
        ubulk = args.audio_dir / f"{stem}.ubulk"
        if not uasset.exists() or not ubulk.exists():
            print(f"missing: {uasset.name} / {ubulk.name}", file=sys.stderr)
            failures += 1
            continue
        try:
            info = read_audio_info(uasset)
            blocks = parse_ubulk(ubulk)
            binka = build_binka(info, blocks)
            out_wav = sounds_dir / f"{event.replace('.', '_')}.wav"
            decode(args.ffmpeg, binka, out_wav)
            entries.append((event, f"ow/{out_wav.name}"))
            print(f"{event}: {stem} -> {out_wav} ({info.sample_rate} Hz, {info.total_samples} samples)")
        except Exception as exc:  # noqa: BLE001 - report each asset and keep going
            print(f"{event}: {exc}", file=sys.stderr)
            failures += 1

    if entries:
        update_manifest(manifest, entries)
        print(f"manifest: {manifest} ({len(entries)} ow event(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
