#!/usr/bin/env python3
"""Convert the legacy ``mc_er_*.txt`` sentinels into a single ``mc_er.ini``.

The adapter reads both formats: an ini key wins, and a missing ini falls back
to the legacy files. This tool is the migration path for an existing install.

Usage:
    uv run python tools/migrate_config.py --game-dir "<ELDEN RING>/Game"
    uv run python tools/migrate_config.py --game-dir ... --remove-legacy
    uv run python tools/migrate_config.py --game-dir ... --dry-run
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List, Optional, Tuple

CORE_GATES: Tuple[Tuple[str, str], ...] = (
    ("overlay", "mc_er_overlay.txt"),
    ("input", "mc_er_input.txt"),
    ("damage", "mc_er_damage.txt"),
    ("bulletfire", "mc_er_bulletfire.txt"),
)
MC_FILE = "mc_er_steve.txt"
OW_FILE = "mc_er_ow.txt"
MODE_FILE = "mc_er_mode.txt"
SUMMON_FILE = "mc_er_summon.txt"
INI_FILE = "mc_er.ini"

KNOWN_MOBS = ("creeper", "skeleton", "iron_golem", "wolf", "zombie")


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def parse_key_values(text: str) -> List[Tuple[str, str]]:
    """``key=value`` lines in file order; comments and blanks are skipped."""
    entries: List[Tuple[str, str]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        key, sep, value = line.partition("=")
        if not sep:
            continue
        key = key.strip().lower()
        if not key:
            continue
        value = value.strip()
        # Drop an inline comment that starts a token, matching the C++ parser.
        for marker in ("#", ";"):
            index = value.find(marker)
            if index > 0 and value[index - 1].isspace():
                value = value[:index].strip()
                break
        entries.append((key, value))
    return entries


def legacy_mode(text: str) -> str:
    """The old reader defaulted an empty/partial mode file to MC."""
    for key, value in parse_key_values(text):
        if key == "start_mode":
            return value.strip().lower()
    return "mc"


def summon_mob(text: str) -> str:
    content = text.lower()
    for mob in KNOWN_MOBS:
        if mob in content:
            return mob
    if "golem" in content:
        return "iron_golem"
    # The old reader defaulted to a creeper for any other (or empty) file.
    return "creeper"


def build_ini(game_dir: Path) -> str:
    """Render the ini for the legacy files that exist in ``game_dir``."""
    names = {path.name for path in game_dir.iterdir() if path.is_file()}

    core: List[Tuple[str, str]] = []
    for key, filename in CORE_GATES:
        if filename in names:
            core.append((key, "1"))
    if MODE_FILE in names:
        mode = legacy_mode(_read_text(game_dir / MODE_FILE))
        if mode in ("mc", "cs", "ow"):
            core.append(("mode", mode))

    mc: List[Tuple[str, str]] = []
    if MC_FILE in names:
        mc.extend(parse_key_values(_read_text(game_dir / MC_FILE)))
    if SUMMON_FILE in names:
        mc.append(("summon_mob", summon_mob(_read_text(game_dir / SUMMON_FILE))))

    ow: List[Tuple[str, str]] = []
    if OW_FILE in names:
        ow.extend(parse_key_values(_read_text(game_dir / OW_FILE)))

    if not core and not mc and not ow:
        raise ValueError(f"no legacy mc_er_* config files found in {game_dir}")

    lines: List[str] = [
        "# Elden Ring adapter configuration (migrated by tools/migrate_config.py).",
        "# mc_er.ini wins over the legacy mc_er_*.txt files; keep only one source.",
    ]
    for section, entries in (("core", core), ("mc", mc), ("ow", ow)):
        if not entries:
            continue
        lines.append("")
        lines.append(f"[{section}]")
        for key, value in entries:
            lines.append(f"{key}={value}")
    return "\n".join(lines) + "\n"


def migrate(
    game_dir: Path,
    *,
    overwrite: bool = False,
    remove_legacy: bool = False,
    dry_run: bool = False,
) -> Path:
    game_dir = Path(game_dir).resolve()
    if not game_dir.is_dir():
        raise FileNotFoundError(f"game directory not found: {game_dir}")

    target = game_dir / INI_FILE
    if target.exists() and not overwrite and not dry_run:
        raise FileExistsError(f"{target} already exists; pass --force to overwrite")

    text = build_ini(game_dir)
    if dry_run:
        sys.stdout.write(text)
        return target

    target.write_text(text, encoding="utf-8")

    if remove_legacy:
        for _, filename in CORE_GATES:
            _retire(game_dir / filename)
        for filename in (MODE_FILE, MC_FILE, OW_FILE, SUMMON_FILE):
            _retire(game_dir / filename)
    return target


def _retire(path: Path) -> None:
    if not path.is_file():
        return
    backup = path.with_name(path.name + ".bak_migrated")
    suffix = 1
    while backup.exists():
        backup = path.with_name(f"{path.name}.bak_migrated.{suffix}")
        suffix += 1
    path.rename(backup)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-dir", required=True, help="Elden Ring Game directory (contains mc_er_*.txt)")
    parser.add_argument("--force", action="store_true", help="overwrite an existing mc_er.ini")
    parser.add_argument(
        "--remove-legacy",
        action="store_true",
        help="rename the migrated mc_er_*.txt files to *.bak_migrated",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the ini and do not write anything")
    args = parser.parse_args(argv)

    try:
        target = migrate(
            Path(args.game_dir),
            overwrite=args.force,
            remove_legacy=args.remove_legacy,
            dry_run=args.dry_run,
        )
    except (FileNotFoundError, FileExistsError, ValueError) as error:
        parser.error(str(error))

    if not args.dry_run:
        print(f"wrote {target}")
        if args.remove_legacy:
            print("legacy files renamed to *.bak_migrated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
