"""Bake 16x16 and 9x9 Minecraft UI textures into C++ header with raw RGBA arrays."""
from pathlib import Path
from PIL import Image


def generate_header():
    tex_dir = Path("assets/source/textures")
    out_header = Path("adapters/sekiro/include/baked_textures.hpp")

    textures = {
        "diamond_sword": tex_dir / "diamond_sword.png",
        "diamond_pickaxe": tex_dir / "diamond_pickaxe.png",
        "dirt": tex_dir / "dirt.png",
        "stone": tex_dir / "stone.png",
        "tnt": tex_dir / "tnt.png",
    }

    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace mc::ui {",
        ""
    ]

    for name, path in textures.items():
        if not path.exists():
            continue
        im = Image.open(path).convert("RGBA")
        w, h = im.size
        pixels = list(im.getdata())
        lines.append(f"inline constexpr uint32_t k_{name}_w = {w};")
        lines.append(f"inline constexpr uint32_t k_{name}_h = {h};")
        lines.append(f"inline const uint32_t k_{name}_pixels[{w * h}] = {{")
        chunk = []
        for r, g, b, a in pixels:
            # 0xAABBGGRR for DXGI_FORMAT_R8G8B8A8_UNORM little-endian
            val = (a << 24) | (b << 16) | (g << 8) | r
            chunk.append(f"0x{val:08X}u")
            if len(chunk) >= 8:
                lines.append("    " + ", ".join(chunk) + ",")
                chunk = []
        if chunk:
            lines.append("    " + ", ".join(chunk) + ",")
        lines.append("};")
        lines.append("")

    # Add authentic 9x9 pixel red heart
    # . X X . . X X . .
    # X R R X X R R X .
    # X R R R R R R X .
    # X R R R R R R X .
    # . X R R R R X . .
    # . . X R R X . . .
    # . . . X X . . . .
    heart_pixels = [
        0, 1, 1, 0, 0, 1, 1, 0, 0,
        1, 2, 2, 1, 1, 2, 2, 1, 0,
        1, 2, 2, 2, 2, 2, 2, 1, 0,
        1, 2, 2, 2, 2, 2, 2, 1, 0,
        0, 1, 2, 2, 2, 2, 1, 0, 0,
        0, 0, 1, 2, 2, 1, 0, 0, 0,
        0, 0, 0, 1, 1, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0
    ]
    lines.append("inline constexpr uint32_t k_heart_w = 9;")
    lines.append("inline constexpr uint32_t k_heart_h = 9;")
    lines.append("inline const uint32_t k_heart_pixels[81] = {")
    chunk = []
    for p in heart_pixels:
        if p == 1:
            val = 0xFF000000 # Black outline
        elif p == 2:
            val = 0xFF0000EE # Bright red heart fill
        else:
            val = 0x00000000 # Transparent
        chunk.append(f"0x{val:08X}u")
        if len(chunk) >= 9:
            lines.append("    " + ", ".join(chunk) + ",")
            chunk = []
    lines.append("};")
    lines.append("")

    # Add authentic 9x9 pixel golden drumstick (hunger)
    drum_pixels = [
        0, 0, 0, 0, 1, 1, 1, 0, 0,
        0, 0, 0, 1, 2, 2, 2, 1, 0,
        0, 0, 1, 2, 2, 2, 2, 1, 0,
        0, 1, 2, 2, 2, 2, 2, 1, 0,
        0, 1, 2, 2, 2, 2, 1, 0, 0,
        0, 0, 1, 2, 2, 1, 0, 0, 0,
        1, 1, 0, 1, 1, 0, 0, 0, 0,
        1, 3, 1, 0, 0, 0, 0, 0, 0,
        0, 1, 1, 0, 0, 0, 0, 0, 0
    ]
    lines.append("inline constexpr uint32_t k_drumstick_w = 9;")
    lines.append("inline constexpr uint32_t k_drumstick_h = 9;")
    lines.append("inline const uint32_t k_drumstick_pixels[81] = {")
    chunk = []
    for p in drum_pixels:
        if p == 1:
            val = 0xFF000000 # Black outline
        elif p == 2:
            val = 0xFF1E82D4 # Golden brown meat
        elif p == 3:
            val = 0xFFD8D8D8 # Bone
        else:
            val = 0x00000000 # Transparent
        chunk.append(f"0x{val:08X}u")
        if len(chunk) >= 9:
            lines.append("    " + ", ".join(chunk) + ",")
            chunk = []
    lines.append("};")
    lines.append("")

    lines.append("} // namespace mc::ui")

    out_header.parent.mkdir(parents=True, exist_ok=True)
    out_header.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated baked textures: {out_header} ({len(lines)} lines)")


if __name__ == "__main__":
    generate_header()
