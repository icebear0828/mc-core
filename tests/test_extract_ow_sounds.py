from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from extract_ow_sounds import (  # noqa: E402
    AudioInfo,
    Block,
    build_binka,
    parse_ubulk,
    read_audio_info,
    update_manifest,
)


def _uasset(tmp_path: Path, rate: int = 48000, channels: int = 1, samples: int = 5000) -> Path:
    header = struct.pack("<4sBBHIIHHIHH", b"ABEU", 1, channels, 0, rate, samples, 920, 1, 1000, 2, 1)
    path = tmp_path / "MIX_test.uasset"
    path.write_bytes(b"\x00" * 16 + header + b"\x00" * 8)
    return path


def _ubulk(tmp_path: Path, blocks: list[bytes], trim: int | None = None) -> Path:
    data = bytearray(b"SEEK")
    data += b"\x00\x80\x07\x00"
    data += struct.pack(">I", len(blocks))
    data += b"\x00\x00\x00\x00"
    data += b"\x00" * (2 * len(blocks) + 2)  # seek table + blocks_per_entry, ignored by the parser
    for i, payload in enumerate(blocks):
        is_last = i == len(blocks) - 1
        if is_last and trim is not None:
            data += struct.pack("<I", 0xFFFF9999)
            data += struct.pack("<HH", len(payload), trim)
        else:
            data += struct.pack("<I", (len(payload) << 16) | 0x9999)
        data += payload
    path = tmp_path / "MIX_test.ubulk"
    path.write_bytes(bytes(data))
    return path


def test_read_audio_info(tmp_path: Path):
    info = read_audio_info(_uasset(tmp_path, rate=44100, channels=2, samples=12345))
    assert info == AudioInfo(channels=2, sample_rate=44100, total_samples=12345)


def test_parse_ubulk_plain_and_trimmed_blocks(tmp_path: Path):
    blocks = parse_ubulk(_ubulk(tmp_path, [b"abc", b"de"], trim=100))
    assert [b.payload for b in blocks] == [b"abc", b"de"]
    assert blocks[0].trim_samples is None
    assert blocks[1].trim_samples == 100


def test_parse_ubulk_rejects_bad_magic(tmp_path: Path):
    path = tmp_path / "bad.ubulk"
    path.write_bytes(b"NOPE" + b"\x00" * 32)
    try:
        parse_ubulk(path)
    except ValueError as exc:
        assert "SEEK" in str(exc)
    else:
        raise AssertionError("expected ValueError")


def test_build_binka_layout():
    info = AudioInfo(channels=1, sample_rate=48000, total_samples=5000)
    blocks = [Block(payload=b"abc"), Block(payload=b"de", trim_samples=100)]
    data = build_binka(info, blocks)

    assert data[:4] == b"ABEU"
    assert data[4] == 1 and data[5] == 1
    rate, samples = struct.unpack_from("<II", data, 8)
    assert rate == 48000 and samples == 5000
    entries = struct.unpack_from("<H", data, 24)[0]
    assert entries == 2
    # Header is 28 bytes, then the 16-bit seek table, then packets.
    packets = 28 + 2 * entries
    assert data[packets:packets + 4] == b"\x99\x99" + struct.pack("<H", 3)
    second = packets + 2 + 2 + 3
    assert data[second:second + 8] == b"\x99\x99" + struct.pack("<HHH", 0xFFFF, 2, 100)


def test_update_manifest_replaces_only_ow_lines(tmp_path: Path):
    manifest = tmp_path / "sounds_manifest.txt"
    manifest.write_text("mc.step|mc/step.wav|1.0|1.0|1.0\now.old|ow/old.wav|1.0|1.0|1.0\n", encoding="utf-8")

    update_manifest(manifest, [("ow.cannon.fire", "ow/ow_cannon_fire.wav")])
    first = manifest.read_text(encoding="utf-8").splitlines()
    assert "mc.step|mc/step.wav|1.0|1.0|1.0" in first
    assert not any(line.startswith("ow.old|") for line in first)
    assert "ow.cannon.fire|ow/ow_cannon_fire.wav|1.0|1.0|1.0" in first

    update_manifest(manifest, [("ow.cannon.fire", "ow/ow_cannon_fire.wav")])
    second = manifest.read_text(encoding="utf-8").splitlines()
    assert second.count("ow.cannon.fire|ow/ow_cannon_fire.wav|1.0|1.0|1.0") == 1
