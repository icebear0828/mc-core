from __future__ import annotations

from pathlib import Path

import pytest

from tools.migrate_config import build_ini, migrate, parse_key_values, summon_mob


def _write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def test_build_ini_converts_gates_mode_and_tuning(tmp_path: Path) -> None:
    _write(tmp_path / "mc_er_overlay.txt", "")
    _write(tmp_path / "mc_er_input.txt", "")
    _write(tmp_path / "mc_er_mode.txt", "start_mode=ow\n")
    _write(tmp_path / "mc_er_steve.txt", "# comment\nhide_native=1\nreach=4.5\n")
    _write(tmp_path / "mc_er_ow.txt", "ow_fov=70\n")
    _write(tmp_path / "mc_er_summon.txt", "skeleton\n")

    text = build_ini(tmp_path)

    assert "[core]" in text
    assert "overlay=1" in text
    assert "input=1" in text
    assert "damage=1" not in text  # no damage sentinel
    assert "mode=ow" in text
    assert "[mc]" in text
    assert "hide_native=1" in text
    assert "reach=4.5" in text
    assert "summon_mob=skeleton" in text
    assert "[ow]" in text
    assert "ow_fov=70" in text


def test_build_ini_omits_absent_sections(tmp_path: Path) -> None:
    _write(tmp_path / "mc_er_overlay.txt", "")

    text = build_ini(tmp_path)

    assert "[core]" in text
    assert "overlay=1" in text
    assert "[mc]" not in text
    assert "[ow]" not in text


def test_build_ini_requires_at_least_one_legacy_file(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        build_ini(tmp_path)


def test_migrate_writes_the_ini_and_refuses_to_overwrite(tmp_path: Path) -> None:
    _write(tmp_path / "mc_er_overlay.txt", "")

    target = migrate(tmp_path)
    assert target.is_file()
    assert "overlay=1" in target.read_text(encoding="utf-8")

    with pytest.raises(FileExistsError):
        migrate(tmp_path)

    migrate(tmp_path, overwrite=True)


def test_migrate_remove_legacy_renames_consumed_files(tmp_path: Path) -> None:
    _write(tmp_path / "mc_er_overlay.txt", "")
    _write(tmp_path / "mc_er_steve.txt", "reach=4\n")
    _write(tmp_path / "mc_er_ow.txt", "ow_fov=70\n")

    migrate(tmp_path, remove_legacy=True)

    assert (tmp_path / "mc_er.ini").is_file()
    assert not (tmp_path / "mc_er_overlay.txt").exists()
    assert (tmp_path / "mc_er_overlay.txt.bak_migrated").is_file()
    assert (tmp_path / "mc_er_steve.txt.bak_migrated").is_file()
    assert (tmp_path / "mc_er_ow.txt.bak_migrated").is_file()


def test_dry_run_writes_nothing(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    _write(tmp_path / "mc_er_overlay.txt", "")

    target = migrate(tmp_path, dry_run=True)

    assert not target.exists()
    assert "overlay=1" in capsys.readouterr().out


def test_parse_key_values_matches_the_cpp_parser() -> None:
    entries = parse_key_values("# comment\nreach = 4 ; note\nhex=#ff00ff\nbroken\n")

    assert entries == [("reach", "4"), ("hex", "#ff00ff")]


def test_legacy_mode_defaults_to_mc() -> None:
    from tools.migrate_config import legacy_mode

    assert legacy_mode("") == "mc"
    assert legacy_mode("# empty\n") == "mc"
    assert legacy_mode("start_mode=cs\n") == "cs"


def test_summon_mob_aliases() -> None:
    assert summon_mob("") == "creeper"
    assert summon_mob("iron golem") == "iron_golem"
    assert summon_mob("golem") == "iron_golem"
    assert summon_mob("Wolf") == "wolf"
