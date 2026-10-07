"""Pure-logic tests for tools/reverse (the process-attaching parts only run on Windows against a game)."""
from __future__ import annotations

import math
import py_compile
import struct
import sys
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent.parent / "tools" / "reverse"
sys.path.insert(0, str(TOOLS))

import memlib  # noqa: E402
import ptrtree  # noqa: E402


@pytest.mark.parametrize("script", sorted(p.name for p in TOOLS.glob("*.py")))
def test_every_tool_compiles(script, tmp_path):
    py_compile.compile(str(TOOLS / script), cfile=str(tmp_path / "x.pyc"), doraise=True)


def test_memlib_refuses_to_attach_off_windows():
    if memlib.IS_WINDOWS:
        pytest.skip("attach is exercised against a real game on Windows")
    with pytest.raises(OSError):
        memlib.Proc(1)


def test_aob_wildcards_match_any_byte_and_nothing_else():
    rx = memlib.aob("48 8B ?? 05 ? FF")
    assert rx.search(b"\x00\x48\x8B\x77\x05\x99\xFF\x00")
    assert not rx.search(b"\x48\x8B\x77\x06\x99\xFF")
    assert not rx.search(b"\x48\x8B\x77\x05\x99\xFE")


def test_aob_matches_newline_bytes():
    assert memlib.aob("0A ?? 0A").search(b"\x0a\x0a\x0a")  # re.S: a wildcard must match 0x0A too


def test_rip_target_follows_the_relative_displacement():
    image = bytearray(0x400)
    struct.pack_into("<i", image, 0x10 + 3, 0x100)          # mov rax,[rip+0x100] at 0x10
    assert memlib.rip_target(bytes(image), 0x10) == 0x10 + 7 + 0x100
    struct.pack_into("<i", image, 0x20 + 3, -0x20)
    assert memlib.rip_target(bytes(image), 0x20) == 0x20 + 7 - 0x20


def _node(floats_at: dict[int, tuple[float, ...]], size: int = 0x200) -> bytes:
    data = bytearray(size)
    for off, values in floats_at.items():
        struct.pack_into(f"<{len(values)}f", data, off, *values)
    return bytes(data)


def test_find_moving_triples_picks_a_walking_position_and_ignores_static_data():
    a = {(): (0x1000, _node({0x40: (103.0, -36.0, 5.0), 0x80: (1.0, 2.0, 3.0)})), (0x20,): (0x2000, _node({0x40: (103.0, -36.0, 5.0)}))}
    b = {(): (0x1000, _node({0x40: (108.0, -35.9, 1.0), 0x80: (1.0, 2.0, 3.0)})), (0x20,): (0x2000, _node({0x40: (108.0, -35.9, 1.0)}))}
    hits = ptrtree.find_moving_triples(a, b)
    positions = {(path, off) for path, off, _x, _y in hits}
    assert ((), 0x40) in positions and ((0x20,), 0x40) in positions     # the copies show up on every path
    assert not any(off == 0x80 for _path, off, _x, _y in hits)            # unchanged data is not a candidate


def test_find_moving_triples_rejects_teleports_and_non_finite_values():
    a = {(): (1, _node({0x40: (0.0, 0.0, 0.0), 0x80: (1.0, 2.0, 3.0)}))}
    b = {(): (1, _node({0x40: (9000.0, 9000.0, 9000.0), 0x80: (math.nan, 2.0, 3.0)}))}
    assert ptrtree.find_moving_triples(a, b) == []


def test_find_rotating_unit_vectors_reports_only_unit_length_triples_that_turned():
    a = {(): (1, _node({0x40: (1.0, 0.0, 0.0), 0x60: (0.0, 1.0, 0.0), 0x80: (3.0, 0.0, 0.0)}))}
    s = math.sqrt(0.5)
    b = {(): (1, _node({0x40: (s, 0.0, s), 0x60: (0.0, 1.0, 0.0), 0x80: (0.0, 3.0, 0.0)}))}
    hits = ptrtree.find_rotating_unit_vectors(a, b)
    assert [(path, off) for path, off, *_ in hits] == [((), 0x40)]
    assert hits[0][4] == pytest.approx(math.pi / 4, abs=1e-4)


def test_snapshot_pickle_roundtrip(tmp_path):
    snap = {(): (0x1000, b"\x01\x02\x03"), (0x48,): (0x2000, b"\x04")}
    path = tmp_path / "s.pkl"
    ptrtree.save(path, snap)
    assert ptrtree.load(path) == snap


def test_project_box_centres_a_box_straight_ahead_and_rejects_one_behind():
    pytest.importorskip("PIL")
    import screen_probe

    rows = ((1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0))   # at the origin looking along +Z
    x0, y0, x1, y1 = screen_probe.project_box(rows, 1.0, 16 / 9, (1920, 1080), (0.0, -0.9, 5.0), pad=0)
    assert abs((x0 + x1) / 2 - 960) < 2                    # horizontally centred
    assert y1 > y0 and 0 <= y0 and y1 <= 1080
    with pytest.raises(ValueError):
        screen_probe.project_box(rows, 1.0, 16 / 9, (1920, 1080), (0.0, 0.0, -5.0))


def test_score_is_zero_for_identical_images_and_grows_with_difference():
    Image = pytest.importorskip("PIL.Image")
    import screen_probe

    a = Image.new("RGB", (40, 40), (10, 10, 10))
    b = Image.new("RGB", (40, 40), (60, 10, 10))
    assert screen_probe.score(a, a, (0, 0, 40, 40)) == 0
    assert screen_probe.score(a, b, (0, 0, 40, 40)) == pytest.approx(50 / 3)
