"""Remote screenshot + image-diff helpers for experiments that need to *see* the game.

The injected overlay saves the frame being presented to <game dir>/mc_screenshot.png when F7 is pressed
or when a file named mc_cmd_screenshot.txt appears in the game folder (an ssh session cannot press keys
in the interactive desktop). These helpers use the file trigger.

Rules that cost real time to learn:
  * measure the baseline noise first (two shots of the unchanged scene) and set the threshold from it;
  * a dark character on a dark background barely moves a pixel-difference score (a model that had
    completely vanished scored only 2.8 against a noise floor of 0.5): always LOOK at the saved image;
  * restore anything you changed right after the shot.
"""
from __future__ import annotations

import math
import os
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


def shoot(game_dir: Path, timeout: float = 4.0) -> Image.Image | None:
    shot = game_dir / "mc_screenshot.png"
    old = shot.stat().st_mtime if shot.exists() else 0.0
    (game_dir / "mc_cmd_screenshot.txt").write_text("1")
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.05)
        if shot.exists() and shot.stat().st_mtime > old:
            time.sleep(0.2)  # let the encoder finish
            try:
                img = Image.open(shot).convert("RGB")
                img.load()
                return img
            except Exception:
                continue
    return None


def project_box(camera_rows, fov_y: float, aspect: float, screen: tuple[int, int], centre, half_extent=(0.55, 1.95, 0.55), pad=10):
    """Screen rectangle of a box standing at `centre` (feet position), from a row-major camera matrix.

    camera_rows = (right, up, forward, position) as float triples; centre is the feet point in the same
    space. Returns (x0, y0, x1, y1) clipped to the screen.
    """
    right, up, fwd, pos = camera_rows
    ys = 1 / math.tan(fov_y / 2)
    xs = ys / aspect
    w, h = screen
    pts = []
    for dx in (-half_extent[0], half_extent[0]):
        for dy in (0.0, half_extent[1]):
            for dz in (-half_extent[2], half_extent[2]):
                q = (centre[0] + dx, centre[1] + dy, centre[2] + dz)
                d = [a - b for a, b in zip(q, pos)]
                x = sum(a * b for a, b in zip(d, right))
                y = sum(a * b for a, b in zip(d, up))
                z = sum(a * b for a, b in zip(d, fwd))
                if z > 0.05:
                    pts.append(((x * xs / z * 0.5 + 0.5) * w, (1 - (y * ys / z * 0.5 + 0.5)) * h))
    if not pts:
        raise ValueError("box is behind the camera")
    return (max(0, int(min(p[0] for p in pts)) - pad), max(0, int(min(p[1] for p in pts)) - pad),
            min(w, int(max(p[0] for p in pts)) + pad), min(h, int(max(p[1] for p in pts)) + pad))


def score(a: Image.Image, b: Image.Image, box) -> float:
    """Mean absolute per-channel difference inside `box` (0..255)."""
    diff = ImageChops.difference(a.crop(box), b.crop(box))
    return sum(ImageStat.Stat(diff).mean) / 3.0


def baseline_noise(game_dir: Path, box, shots: int = 3) -> tuple[list[Image.Image], float]:
    imgs = [shoot(game_dir) for _ in range(shots)]
    if not all(imgs):
        raise RuntimeError("screenshot trigger did not work (is the overlay DLL loaded and the game in a world?)")
    noise = max(score(imgs[i], imgs[i + 1], box) for i in range(shots - 1))
    return imgs, noise
