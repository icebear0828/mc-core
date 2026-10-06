"""Inspect texture files in assets/source/textures."""
from pathlib import Path
from PIL import Image


def main():
    tex_dir = Path("assets/source/textures")
    for p in sorted(tex_dir.glob("*.png")):
        im = Image.open(p)
        print(f"Texture: {p.name:20s} Size: {im.size} Mode: {im.mode}")


if __name__ == "__main__":
    main()
