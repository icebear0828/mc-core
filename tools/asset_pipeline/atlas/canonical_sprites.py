"""Canonical placeholder sprites used when no official client.jar is available."""
from __future__ import annotations

from PIL import Image

_PLACEHOLDER_ITEM_COLORS = {
    "item_arrow": (200, 200, 200), "item_trident": (40, 150, 150), "item_flint_and_steel": (90, 90, 90),
    "item_ender_pearl": (20, 90, 80), "item_enchanted_golden_apple": (200, 120, 230), "item_bread": (200, 150, 70),
    "item_cooked_beef": (120, 60, 30), "item_firework_rocket": (200, 40, 40), "item_zombie_spawn_egg": (0, 175, 175),
    "item_creeper_spawn_egg": (13, 167, 0), "item_skeleton_spawn_egg": (193, 193, 193),
    "block_dirt": (134, 96, 67), "block_stone": (125, 125, 125), "block_tnt_top": (160, 80, 70),
    "block_tnt_side": (200, 60, 50), "block_tnt_bottom": (160, 80, 70),
    # Mining loot (issue #81): flat placeholder colours, replaced by the client.jar icons.
    "item_coal": (40, 40, 40), "item_raw_copper": (200, 110, 60), "item_raw_iron": (205, 170, 140),
    "item_redstone": (200, 30, 30), "item_raw_gold": (235, 200, 60), "item_lapis_lazuli": (40, 80, 200),
    "item_amethyst_shard": (160, 100, 210), "item_diamond": (90, 220, 220), "item_emerald": (40, 200, 90),
    "item_netherite_scrap": (110, 75, 65), "item_cobblestone": (120, 120, 120),
    # Pickaxe tiers (issue #81): flat placeholder colours, replaced by the client.jar icons.
    "item_wooden_pickaxe": (140, 100, 50), "item_stone_pickaxe": (130, 130, 130),
    "item_iron_pickaxe": (210, 210, 215), "item_golden_pickaxe": (250, 210, 60),
    "item_netherite_pickaxe": (80, 70, 75),
    "block_cobblestone": (120, 120, 120),
    "block_wood_top": (160, 120, 70), "block_wood_side": (120, 85, 45),
    "block_obsidian": (35, 25, 50),
    "item_wood": (140, 100, 50), "item_obsidian": (45, 35, 65),
    # Crafting items (issue #67): flat placeholders, replaced by the client.jar icons.
    "item_wooden_sword": (140, 100, 50), "item_stone_sword": (130, 130, 130),
    "item_iron_sword": (210, 210, 215), "item_netherite_sword": (80, 70, 75),
    "item_crafting_table": (160, 120, 70), "item_anvil": (80, 80, 85),
    "item_wood_planks": (160, 120, 70), "item_sand": (220, 210, 160),
    "item_stick": (140, 100, 50), "item_iron_ingot": (210, 210, 215),
    "item_gold_ingot": (250, 210, 60), "item_netherite_ingot": (80, 70, 75),
    "item_string": (220, 220, 220), "item_gunpowder": (90, 90, 90),
    "item_flint": (60, 60, 65), "item_apple": (220, 30, 30),
    # The crafting table's block faces (the item icon above is the isometric cube).
    "block_crafting_table_top": (160, 120, 70), "block_crafting_table_side": (140, 100, 50),
    "block_crafting_table_front": (150, 110, 55),
    # Buildable blocks: face textures and item icons.
    "block_sand": (220, 210, 160), "block_gravel": (130, 130, 130), "block_planks": (160, 120, 70),
    "block_stone_bricks": (120, 120, 120), "block_bricks": (150, 70, 60), "block_white_wool": (230, 230, 230),
    "item_gravel": (130, 130, 130), "item_stone_bricks": (120, 120, 120), "item_bricks": (150, 70, 60),
    "item_white_wool": (230, 230, 230),
    "block_anvil_top": (80, 80, 85), "block_anvil_side": (70, 70, 75),
}


def create_canonical_sprite(name: str) -> Image.Image:
    """Generate canonical pixel sprite when extracting without an official client.jar."""
    if name == "entity_shield":
        # Placeholder with the real texture's layout (ShieldModel: plate box at texOffs (0, 0), 12x22x1; handle box at (26, 0), 2x6x6):
        # a riveted front at (1, 1), a plain back at (14, 1), iron edges around them and a dark handle.
        sheet = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        iron, iron_dk = (150, 150, 155, 255), (95, 95, 100, 255)
        wood, wood_dk, rivet = (140, 95, 50, 255), (112, 74, 36, 255), (200, 200, 205, 255)
        for x in range(0, 27):  # the top and bottom edge strips and the side strips of the plate
            for y in range(0, 24):
                if y in (0, 23) or x in (0, 13, 26):
                    sheet.putpixel((x, y), iron if (x + y) % 2 == 0 else iron_dk)
        for face_x in (1, 14):
            for dx in range(12):
                for dy in range(22):
                    rim = dx in (0, 11) or dy in (0, 21)
                    col = iron if rim else (wood if dx % 3 else wood_dk)
                    if face_x == 1 and not rim and dx in (2, 9) and dy % 4 == 2:
                        col = rivet  # the front has rivets, the back does not
                    sheet.putpixel((face_x + dx, 1 + dy), col)
        for x in range(26, 42):  # the handle's six faces
            for y in range(0, 12):
                sheet.putpixel((x, y), iron_dk if (x + y) % 3 else wood_dk)
        return sheet

    if name == "crosshair":
        img = Image.new("RGBA", (15, 15), (0, 0, 0, 0))
        for i in range(4, 11):  # a plain white plus; the real sprite replaces it when a client.jar is given
            img.putpixel((7, i), (255, 255, 255, 220))
            img.putpixel((i, 7), (255, 255, 255, 220))
        return img

    if name in ("heart_container", "heart_half", "hunger_container", "hunger_half"):
        # Derived from the full sprite so the placeholder set stays visually consistent
        base = create_canonical_sprite("heart_full" if name.startswith("heart") else "hunger_full")
        out = Image.new("RGBA", base.size, (0, 0, 0, 0))
        for y in range(base.height):
            for x in range(base.width):
                r, g, b, a = base.getpixel((x, y))
                if a == 0:
                    continue
                if name.endswith("container"):
                    out.putpixel((x, y), (r, g, b, a) if (r, g, b) == (0, 0, 0) else (50, 50, 50, 255))
                elif x < base.width // 2 + 1:
                    out.putpixel((x, y), (r, g, b, a))
        return out

    if name == "heart_full":
        img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
        c_blk, c_red, c_wht, c_drk = (0, 0, 0, 255), (235, 18, 18, 255), (255, 255, 255, 255), (145, 0, 0, 255)
        pixels = {
            (1, 0): c_blk, (2, 0): c_blk, (5, 0): c_blk, (6, 0): c_blk,
            (0, 1): c_blk, (1, 1): c_red, (2, 1): c_wht, (3, 1): c_blk, (4, 1): c_blk, (5, 1): c_red, (6, 1): c_red, (7, 1): c_blk,
            (0, 2): c_blk, (1, 2): c_wht, (2, 2): c_red, (3, 2): c_red, (4, 2): c_red, (5, 2): c_red, (6, 2): c_drk, (7, 2): c_blk,
            (0, 3): c_blk, (1, 3): c_red, (2, 3): c_red, (3, 3): c_red, (4, 3): c_red, (5, 3): c_red, (6, 3): c_drk, (7, 3): c_blk,
            (1, 4): c_blk, (2, 4): c_red, (3, 4): c_red, (4, 4): c_red, (5, 4): c_drk, (6, 4): c_blk,
            (2, 5): c_blk, (3, 5): c_red, (4, 5): c_drk, (5, 5): c_blk,
            (3, 6): c_blk, (4, 6): c_drk, (5, 6): c_blk,
            (4, 7): c_blk,
        }
        for (x, y), col in pixels.items():
            img.putpixel((x, y), col)
        return img

    if name == "hunger_full":
        img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
        c_blk, c_tan, c_car, c_drk = (35, 15, 5, 255), (225, 149, 79, 255), (189, 107, 34, 255), (109, 57, 14, 255)
        c_bnw, c_bnd = (230, 230, 230, 255), (160, 160, 160, 255)
        pixels = {
            (4, 0): c_blk, (5, 0): c_blk, (6, 0): c_blk,
            (3, 1): c_blk, (4, 1): c_tan, (5, 1): c_car, (6, 1): c_car, (7, 1): c_blk,
            (2, 2): c_blk, (3, 2): c_tan, (4, 2): c_car, (5, 2): c_car, (6, 2): c_drk, (7, 2): c_blk,
            (1, 3): c_blk, (2, 3): c_car, (3, 3): c_car, (4, 3): c_car, (5, 3): c_drk, (6, 3): c_drk, (7, 3): c_blk,
            (0, 4): c_blk, (1, 4): c_car, (2, 4): c_car, (3, 4): c_car, (4, 4): c_drk, (5, 4): c_blk,
            (0, 5): c_blk, (1, 5): c_bnw, (2, 5): c_car, (3, 5): c_drk, (4, 5): c_blk,
            (1, 6): c_blk, (2, 6): c_bnd, (3, 6): c_bnw, (4, 6): c_blk,
            (1, 7): c_blk, (2, 7): c_bnw, (3, 7): c_bnd, (4, 7): c_blk,
            (2, 8): c_blk, (3, 8): c_blk,
        }
        for (x, y), col in pixels.items():
            img.putpixel((x, y), col)
        return img

    if name in ("heart_absorb_full", "heart_absorb_half"):
        base = create_canonical_sprite("heart_full" if name.endswith("full") else "heart_half")
        out = Image.new("RGBA", base.size, (0, 0, 0, 0))
        recolor = {(235, 18, 18): (255, 205, 40), (145, 0, 0): (170, 120, 0)}
        for y in range(base.height):
            for x in range(base.width):
                r, g, b, a = base.getpixel((x, y))
                if a:
                    nr, ng, nb = recolor.get((r, g, b), (r, g, b))
                    out.putpixel((x, y), (nr, ng, nb, a))
        return out

    if name == "particle_crit":
        img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
        for i in range(8):  # a plain four-point star
            img.putpixel((3, i), (255, 255, 255, 255))
            img.putpixel((4, i), (255, 255, 255, 255))
            img.putpixel((i, 3), (255, 255, 255, 255))
            img.putpixel((i, 4), (255, 255, 255, 255))
        return img

    if name == "particle_damage":
        img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
        for (x, y) in [(1, 2), (2, 1), (5, 1), (6, 2), (1, 3), (2, 3), (3, 3), (4, 3), (5, 3), (6, 3), (2, 4), (3, 4), (4, 4), (5, 4), (3, 5), (4, 5)]:
            img.putpixel((x, y), (130, 0, 0, 255))
        return img

    if name.startswith("particle_sweep_"):
        idx = int(name.rsplit("_", 1)[1])
        img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        reach = 4 + idx * 3  # an arc that grows with the frame; the real frames replace it
        for x in range(32):
            for y in range(32):
                d = ((x - 16) ** 2 + (y - 26) ** 2) ** 0.5
                if reach - 1.5 <= d <= reach + 1.5 and y < 26:
                    img.putpixel((x, y), (255, 255, 255, 255 - idx * 20))
        return img

    if name in ("heart_container_blinking", "heart_full_blinking", "heart_half_blinking"):
        base = create_canonical_sprite({"heart_container_blinking": "heart_container", "heart_full_blinking": "heart_full",
                                        "heart_half_blinking": "heart_half"}[name])
        out = Image.new("RGBA", base.size, (0, 0, 0, 0))
        for y in range(base.height):
            for x in range(base.width):
                r, g, b, a = base.getpixel((x, y))
                if a:  # the same shape in white
                    out.putpixel((x, y), (255, 255, 255, a) if (r, g, b) != (0, 0, 0) else (r, g, b, a))
        return out

    if name in ("xp_bar_background", "xp_bar_progress"):
        col = (50, 50, 50, 255) if name == "xp_bar_background" else (128, 255, 32, 255)
        return Image.new("RGBA", (182, 5), col)

    if name in ("container_top", "container_bottom"):
        h = 71 if name == "container_top" else 96
        img = Image.new("RGBA", (176, h), (198, 198, 198, 255))
        rows = [(8 + 0, 18 + 18 * r) for r in range(3)] if name == "container_top" else [(8, 14 + 18 * r) for r in range(3)] + [(8, 72)]
        for sx, sy in rows:
            for c in range(9):
                for dx in range(-1, 17):
                    for dy in range(-1, 17):
                        x, y = sx + 18 * c + dx, sy + dy
                        if 0 <= x < 176 and 0 <= y < h:
                            img.putpixel((x, y), (139, 139, 139, 255))
        return img

    if name == "hotbar":
        img = Image.new("RGBA", (182, 22), (40, 40, 40, 200))
        for x in range(182):
            for y in (0, 21):
                img.putpixel((x, y), (200, 200, 200, 255))
        for i in range(10):
            for y in range(22):
                img.putpixel((min(i * 20 + 1, 181), y), (200, 200, 200, 255))
        return img

    if name == "hotbar_selection":
        img = Image.new("RGBA", (24, 23), (0, 0, 0, 0))
        for x in range(24):
            for y in range(23):
                if x < 2 or x >= 22 or y < 2 or y >= 21:
                    img.putpixel((x, y), (255, 255, 255, 255))
        return img

    if name == "hotbar_offhand_left":
        img = Image.new("RGBA", (29, 24), (0, 0, 0, 0))
        for x in range(29):
            for y in range(24):
                if x == 0 or x == 28 or y == 0 or y == 23:
                    img.putpixel((x, y), (200, 200, 200, 255))
                else:
                    img.putpixel((x, y), (40, 40, 40, 200))
        return img

    # 16x16 Items
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    if name == "item_diamond_sword":
        c_dm, c_dd, c_hi = (43, 235, 235, 255), (28, 163, 163, 255), (191, 255, 248, 255)
        c_wd, c_gd, c_bk = (102, 66, 25, 255), (54, 34, 13, 255), (0, 0, 0, 255)
        img.putpixel((13, 2), c_hi)
        img.putpixel((14, 1), c_bk)
        for d in range(7):
            img.putpixel((12 - d, 3 + d), c_hi)
            img.putpixel((13 - d, 4 + d), c_dm)
            img.putpixel((14 - d, 5 + d), c_dd)
        img.putpixel((6, 11), c_gd); img.putpixel((5, 10), c_gd); img.putpixel((7, 9), c_gd); img.putpixel((8, 10), c_gd)
        img.putpixel((4, 11), c_bk); img.putpixel((4, 12), c_wd); img.putpixel((3, 13), c_wd)
        img.putpixel((2, 14), c_gd); img.putpixel((1, 15), c_bk)
    elif name == "item_diamond_pickaxe":
        c_dm, c_dd, c_wd, c_bk = (43, 235, 235, 255), (28, 163, 163, 255), (102, 66, 25, 255), (0, 0, 0, 255)
        for a in range(5):
            img.putpixel((10 + a, 2 + (a - 2 if a > 2 else 0)), c_dm)
            img.putpixel((5 - a, 7 + (a - 2 if a > 2 else 0)), c_dd)
        for h in range(9):
            img.putpixel((10 - h, 6 + h), c_wd)
        img.putpixel((1, 15), c_bk)
    elif name == "item_dirt":
        c_grs, c_drt = (91, 135, 49, 255), (134, 96, 67, 255)
        for r in range(2, 14):
            for c in range(2, 14):
                img.putpixel((c, r), c_grs if r < 6 else c_drt)
    elif name == "item_stone":
        c_st = (125, 125, 125, 255)
        for r in range(2, 14):
            for c in range(2, 14):
                img.putpixel((c, r), c_st)
    elif name == "item_tnt":
        c_red, c_wht = (219, 51, 31, 255), (240, 240, 240, 255)
        for r in range(2, 14):
            for c in range(2, 14):
                img.putpixel((c, r), c_wht if 6 <= r <= 9 else c_red)
    elif name == "item_golden_apple":
        c_gld, c_stm = (255, 215, 0, 255), (100, 60, 20, 255)
        img.putpixel((8, 2), c_stm); img.putpixel((9, 1), c_stm)
        for y in range(4, 14):
            for x in range(4, 14):
                if (x - 8)**2 + (y - 8)**2 <= 25:
                    img.putpixel((x, y), c_gld)
    elif name == "item_bow":
        c_wd, c_st = (139, 90, 43, 255), (230, 230, 230, 255)
        for b in range(9):
            img.putpixel((12 - abs(b - 4), 3 + b), c_wd)
            img.putpixel((8, 3 + b), c_st)
    elif name.startswith("item_bow_pulling_"):  # the bow with an arrow nocked, the string drawn back further at each of the three stages
        stage = int(name[-1])
        c_wd, c_st, c_ar = (139, 90, 43, 255), (230, 230, 230, 255), (200, 200, 200, 255)
        for b in range(9):
            img.putpixel((12 - abs(b - 4), 3 + b), c_wd)
        for b in range(9):  # the string, bent back by the draw
            img.putpixel((8 - min(stage + 1, 3) * (4 - abs(b - 4)) // 4, 3 + b), c_st)
        for x in range(5 - stage, 13):  # the arrow
            img.putpixel((x, 7), c_ar)
    elif name == "item_elytra":
        c_w1, c_w2 = (108, 98, 117, 255), (76, 68, 84, 255)
        for e in range(10):
            img.putpixel((6 - e // 3, 3 + e), c_w1)
            img.putpixel((9 + e // 3, 3 + e), c_w2)
    elif name == "item_totem_of_undying":
        c_gld, c_emr = (255, 199, 0, 255), (64, 219, 112, 255)
        for y in range(3, 14):
            for x in range(5, 11):
                img.putpixel((x, y), c_gld)
        img.putpixel((6, 5), c_emr); img.putpixel((9, 5), c_emr)
    elif name == "item_shield":
        # Full 16x16 canonical Minecraft shield with iron rim, wood planks, and iron boss/rivet
        c_ir_hi = (220, 220, 225, 255)  # iron highlight
        c_ir_md = (180, 180, 185, 255)  # iron midtone
        c_ir_dk = (120, 120, 125, 255)  # iron shadow
        c_ir_ol = (60, 60, 65, 255)     # iron outline edge
        c_wd_hi = (170, 120, 70, 255)   # wood light grain
        c_wd_md = (145, 95, 50, 255)    # wood base
        c_wd_dk = (115, 75, 35, 255)    # wood seam

        # Shield shape: row ranges [x_start, x_end] (inclusive)
        shield_rows = {
            1: (3, 12),
            2: (2, 13), 3: (2, 13), 4: (2, 13), 5: (2, 13),
            6: (2, 13), 7: (2, 13), 8: (2, 13),
            9: (3, 12), 10: (3, 12),
            11: (4, 11), 12: (4, 11),
            13: (5, 10),
            14: (6, 9),
        }
        for y, (x_min, x_max) in shield_rows.items():
            for x in range(x_min, x_max + 1):
                is_rim = (y == 1 or y == 14 or x == x_min or x == x_max)
                if is_rim:
                    if y == 1 or x == x_min:
                        col = c_ir_hi if (x + y) % 2 == 0 else c_ir_md
                    elif x == x_max or y >= 13:
                        col = c_ir_dk if (x + y) % 2 == 0 else c_ir_ol
                    else:
                        col = c_ir_md
                elif y in (6, 7) and x in (7, 8):
                    # Central iron boss / rivet
                    col = c_ir_hi if (x == 7 and y == 6) else (c_ir_dk if (x == 8 and y == 7) else c_ir_md)
                else:
                    # Wood planks with vertical grain pattern
                    if x in (4, 8, 11):
                        col = c_wd_dk
                    elif (x + y) % 3 == 0:
                        col = c_wd_hi
                    else:
                        col = c_wd_md
                img.putpixel((x, y), col)
    elif name == "item_diamond_helmet":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(4, 13):
            for x in range(3, 13):
                if y <= 8 or x in (3, 4, 11, 12):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name == "item_diamond_chestplate":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(3, 13):
            for x in range(2, 14):
                if (y >= 6 and 5 <= x <= 10) or (x in (2, 3, 4, 11, 12, 13) and y <= 8) or (y <= 5 and (x <= 4 or x >= 11)):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name == "item_diamond_leggings":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(3, 14):
            for x in range(3, 13):
                if y <= 6 or x in (3, 4, 5, 10, 11, 12):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name == "item_diamond_boots":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(7, 13):
            for x in range(2, 14):
                if (x in (2, 3, 4, 5) and (y >= 10 or x <= 4)) or (x in (10, 11, 12, 13) and (y >= 10 or x >= 11)):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name.startswith("empty_armor_slot_"):
        c_wm = (140, 140, 140, 120)
        part = name.replace("empty_armor_slot_", "")
        if part == "helmet":
            for y in range(5, 11):
                for x in range(4, 12):
                    if y <= 8 or x in (4, 5, 10, 11): img.putpixel((x, y), c_wm)
        elif part == "chestplate":
            for y in range(4, 12):
                for x in range(3, 13):
                    if (y >= 6 and 5 <= x <= 10) or (y <= 5 and (x <= 5 or x >= 10)): img.putpixel((x, y), c_wm)
        elif part == "leggings":
            for y in range(4, 13):
                for x in range(4, 12):
                    if y <= 7 or x in (4, 5, 10, 11): img.putpixel((x, y), c_wm)
        elif part == "boots":
            for y in range(7, 12):
                for x in range(3, 13):
                    if x in (3, 4, 5, 10, 11, 12): img.putpixel((x, y), c_wm)
    elif name in ("armor_empty", "armor_half", "armor_full"):
        img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
        c_out = (30, 30, 30, 255)
        c_iron = (230, 230, 230, 255)
        c_iron_d = (160, 160, 160, 255)
        c_bg = (50, 50, 50, 180)
        for y in range(9):
            for x in range(9):
                is_pixel = False
                if y == 0 and (x in (1, 2, 6, 7)): is_pixel = True
                elif y in (1, 2) and (1 <= x <= 7): is_pixel = True
                elif y in (3, 4) and (0 <= x <= 8): is_pixel = True
                elif y in (5, 6) and (1 <= x <= 7): is_pixel = True
                elif y == 7 and (2 <= x <= 6): is_pixel = True
                elif y == 8 and (3 <= x <= 5): is_pixel = True
                if not is_pixel: continue
                is_outline = (y == 0 or y == 8 or x == 0 or x == 8 or
                              (y in (1, 2) and x in (1, 7)) or
                              (y in (3, 4) and x in (0, 8)) or
                              (y in (5, 6) and x in (1, 7)) or
                              (y == 7 and x in (2, 6)) or
                              (y == 8 and x in (3, 5)) or
                              (y == 1 and x in (3, 4, 5)))
                if is_outline:
                    img.putpixel((x, y), c_out)
                else:
                    if name == "armor_empty":
                        img.putpixel((x, y), c_bg)
                    elif name == "armor_full":
                        img.putpixel((x, y), c_iron if (x + y) % 2 == 0 else c_iron_d)
                    elif name == "armor_half":
                        if x <= 4:
                            img.putpixel((x, y), c_iron if (x + y) % 2 == 0 else c_iron_d)
                        else:
                            img.putpixel((x, y), c_bg)
    elif name.startswith("destroy_stage_"):
        stage = int(name.split("_")[-1])
        img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        target_count = 6 + stage * 12
        coords = list(dict.fromkeys([((x * 7 + y * 13) % 16, (x * 11 + y * 3) % 16) for y in range(16) for x in range(16)]))
        for y in range(16):
            for x in range(16):
                if (x, y) not in coords:
                    coords.append((x, y))
        for x, y in coords[:target_count]:
            img.putpixel((x, y), (50, 50, 50, 255))
    elif name.startswith("block_") and name in _PLACEHOLDER_ITEM_COLORS:  # a flat coloured square
        img = Image.new("RGBA", (16, 16), _PLACEHOLDER_ITEM_COLORS[name] + (255,))
    elif name in _PLACEHOLDER_ITEM_COLORS:  # a plain coloured disc; the real icon replaces it when a client.jar is given
        col = _PLACEHOLDER_ITEM_COLORS[name] + (255,)
        for y in range(16):
            for x in range(16):
                if (x - 7.5) ** 2 + (y - 7.5) ** 2 <= 36:
                    img.putpixel((x, y), col)

    return img
