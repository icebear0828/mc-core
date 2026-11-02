"""Constants shared by the geometry, texture, atlas and codegen stages."""
from __future__ import annotations

PARTS = [
    ("head", (0, 0, 0), (-4, -8, -4), (8, 8, 8), (0, 0), 0),
    ("body", (0, 0, 0), (-4, 0, -2), (8, 12, 4), (16, 16), 0),
    ("right_arm", (-5, 2, 0), (-3, -2, -2), (4, 12, 4), (40, 16), 0),
    ("left_arm", (5, 2, 0), (-1, -2, -2), (4, 12, 4), (32, 48), 0),
    ("right_leg", (-1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (0, 16), 0),
    ("left_leg", (1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (16, 48), 0),
    ("hat", (0, 0, 0), (-4, -8, -4), (8, 8, 8), (32, 0), 0.5),
    ("jacket", (0, 0, 0), (-4, 0, -2), (8, 12, 4), (16, 32), 0.25),
    ("right_sleeve", (-5, 2, 0), (-3, -2, -2), (4, 12, 4), (40, 32), 0.25),
    ("left_sleeve", (5, 2, 0), (-1, -2, -2), (4, 12, 4), (48, 48), 0.25),
    ("right_pants", (-1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (0, 32), 0.25),
    ("left_pants", (1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (0, 48), 0.25),
]

_JAR_TEXTURES = "assets/minecraft/textures/"

# (key, native size). The order is the packing order and therefore the UV layout.
HUD_SPRITES: list[tuple[str, tuple[int, int]]] = [
    ("crosshair", (15, 15)),
    ("heart_container", (9, 9)),
    ("heart_full", (9, 9)),
    ("heart_half", (9, 9)),
    ("hunger_container", (9, 9)),
    ("hunger_full", (9, 9)),
    ("hunger_half", (9, 9)),
    ("hotbar", (182, 22)),
    ("hotbar_selection", (24, 23)),
    ("item_diamond_sword", (16, 16)),
    ("item_diamond_pickaxe", (16, 16)),
    ("item_dirt", (16, 16)),
    ("item_stone", (16, 16)),
    ("item_tnt", (16, 16)),
    ("item_golden_apple", (16, 16)),
    ("item_bow", (16, 16)),
    ("item_elytra", (16, 16)),
    ("item_totem_of_undying", (16, 16)),
    # appended after the original layout, so every earlier UV stays exactly where it was
    ("heart_absorb_full", (9, 9)),
    ("heart_absorb_half", (9, 9)),
    ("particle_crit", (8, 8)),
    ("particle_damage", (8, 8)),
    ("particle_sweep_0", (32, 32)),
    ("particle_sweep_1", (32, 32)),
    ("particle_sweep_2", (32, 32)),
    ("particle_sweep_3", (32, 32)),
    ("particle_sweep_4", (32, 32)),
    ("particle_sweep_5", (32, 32)),
    ("particle_sweep_6", (32, 32)),
    ("particle_sweep_7", (32, 32)),
    # the inventory screen: the rest of the 16x16 item icons, and the two halves of the 3-row container background
    ("item_arrow", (16, 16)),
    ("item_trident", (16, 16)),
    ("item_flint_and_steel", (16, 16)),
    ("item_ender_pearl", (16, 16)),
    ("item_enchanted_golden_apple", (16, 16)),
    ("item_bread", (16, 16)),
    ("item_cooked_beef", (16, 16)),
    ("item_firework_rocket", (16, 16)),
    ("container_top", (176, 71)),
    ("container_bottom", (176, 96)),
    # the faces of the blocks placed in the world (flat, as in the jar)
    ("block_dirt", (16, 16)),
    ("block_stone", (16, 16)),
    ("block_tnt_top", (16, 16)),
    ("block_tnt_side", (16, 16)),
    ("block_tnt_bottom", (16, 16)),
    # the heart blink (a hit flashes the container and shows the lost hearts white) and the experience bar
    ("heart_container_blinking", (9, 9)),
    ("heart_full_blinking", (9, 9)),
    ("heart_half_blinking", (9, 9)),
    ("xp_bar_background", (182, 5)),
    ("xp_bar_progress", (182, 5)),
    # the spawn egg of the first mob (appended: the sprites above keep their places)
    ("item_zombie_spawn_egg", (16, 16)),
    # the bow as it looks while it is drawn (the arrow nocked, the string drawn back further in each of the three stages)
    ("item_bow_pulling_0", (16, 16)),
    ("item_bow_pulling_1", (16, 16)),
    ("item_bow_pulling_2", (16, 16)),
    # the offhand hotbar slot and the shield item icon
    ("hotbar_offhand_left", (29, 24)),
    ("item_shield", (16, 16)),
    # the whole 64x64 shield entity texture: the 3D shield model (plate 12x22x1 and handle 2x6x6) is mapped onto it like the game does
    ("entity_shield", (64, 64)),
    # diamond armor items and empty armor slot watermarks
    ("item_diamond_helmet", (16, 16)),
    ("item_diamond_chestplate", (16, 16)),
    ("item_diamond_leggings", (16, 16)),
    ("item_diamond_boots", (16, 16)),
    ("empty_armor_slot_helmet", (16, 16)),
    ("empty_armor_slot_chestplate", (16, 16)),
    ("empty_armor_slot_leggings", (16, 16)),
    ("empty_armor_slot_boots", (16, 16)),
    # HUD armor bar sprites
    ("armor_empty", (9, 9)),
    ("armor_half", (9, 9)),
    ("armor_full", (9, 9)),
    # 10 stages of block destruction (cracking) overlay
    *[(f"destroy_stage_{i}", (16, 16)) for i in range(10)],
    # Additional authentic spawn eggs (appended at the end to keep earlier UV coordinates invariant)
    ("item_creeper_spawn_egg", (16, 16)),
    ("item_skeleton_spawn_egg", (16, 16)),
    # Mining loot icons (issue #81), appended so every earlier UV stays exactly where it was
    ("item_coal", (16, 16)),
    ("item_raw_copper", (16, 16)),
    ("item_raw_iron", (16, 16)),
    ("item_redstone", (16, 16)),
    ("item_raw_gold", (16, 16)),
    ("item_lapis_lazuli", (16, 16)),
    ("item_amethyst_shard", (16, 16)),
    ("item_diamond", (16, 16)),
    ("item_emerald", (16, 16)),
    ("item_netherite_scrap", (16, 16)),
    ("item_cobblestone", (16, 16)),
    # Pickaxe tier icons (issue #81), appended so every earlier UV stays exactly where it was
    ("item_wooden_pickaxe", (16, 16)),
    ("item_stone_pickaxe", (16, 16)),
    ("item_iron_pickaxe", (16, 16)),
    ("item_golden_pickaxe", (16, 16)),
    ("item_netherite_pickaxe", (16, 16)),
    # The cobblestone block's own face texture (the item icon is the 3D cube, like dirt/stone)
    ("block_cobblestone", (16, 16)),
    # Wood (oak log) top/side and obsidian face textures
    ("block_wood_top", (16, 16)),
    ("block_wood_side", (16, 16)),
    ("block_obsidian", (16, 16)),
    ("item_wood", (16, 16)),
    ("item_obsidian", (16, 16)),
    # Crafting items (issue #67), appended so every earlier UV stays exactly where it was.
    # The four block items are drawn as isometric cubes from their block textures.
    ("item_wooden_sword", (16, 16)),
    ("item_stone_sword", (16, 16)),
    ("item_iron_sword", (16, 16)),
    ("item_netherite_sword", (16, 16)),
    ("item_crafting_table", (16, 16)),
    ("item_anvil", (16, 16)),
    ("item_wood_planks", (16, 16)),
    ("item_sand", (16, 16)),
    ("item_stick", (16, 16)),
    ("item_iron_ingot", (16, 16)),
    ("item_gold_ingot", (16, 16)),
    ("item_netherite_ingot", (16, 16)),
    ("item_string", (16, 16)),
    ("item_gunpowder", (16, 16)),
    ("item_flint", (16, 16)),
    ("item_apple", (16, 16)),
    # The crafting table's block faces, appended so every earlier UV stays exactly where it was.
    ("block_crafting_table_top", (16, 16)),
    ("block_crafting_table_side", (16, 16)),
    ("block_crafting_table_front", (16, 16)),
    # Buildable block faces and item icons, appended so every earlier UV stays exactly where it was.
    ("block_sand", (16, 16)),
    ("block_gravel", (16, 16)),
    ("block_planks", (16, 16)),
    ("block_stone_bricks", (16, 16)),
    ("block_bricks", (16, 16)),
    ("block_white_wool", (16, 16)),
    ("item_gravel", (16, 16)),
    ("item_stone_bricks", (16, 16)),
    ("item_bricks", (16, 16)),
    ("item_white_wool", (16, 16)),
    # The anvil's block faces (the item icon above is the isometric cube).
    ("block_anvil_top", (16, 16)),
    ("block_anvil_side", (16, 16)),
    # Non-cube and transparent blocks, appended so every earlier UV stays exactly where it was.
    ("block_glass", (16, 16)),
    ("block_leaves", (16, 16)),
    ("block_stone_slab", (16, 16)),
    ("block_torch", (16, 16)),
    ("item_glass", (16, 16)),
    ("item_leaves", (16, 16)),
    ("item_stone_slab", (16, 16)),
    ("item_torch", (16, 16)),
]


_JAR_HUD_SPRITES = {
    "item_diamond_helmet": "item/diamond_helmet.png",
    "item_diamond_chestplate": "item/diamond_chestplate.png",
    "item_diamond_leggings": "item/diamond_leggings.png",
    "item_diamond_boots": "item/diamond_boots.png",
    "empty_armor_slot_helmet": "gui/sprites/container/slot/helmet.png",
    "empty_armor_slot_chestplate": "gui/sprites/container/slot/chestplate.png",
    "empty_armor_slot_leggings": "gui/sprites/container/slot/leggings.png",
    "empty_armor_slot_boots": "gui/sprites/container/slot/boots.png",
    "armor_empty": "gui/sprites/hud/armor_empty.png",
    "armor_half": "gui/sprites/hud/armor_half.png",
    "armor_full": "gui/sprites/hud/armor_full.png",
    "entity_shield": "entity/shield_base_nopattern.png",
    "crosshair": "gui/sprites/hud/crosshair.png",
    "heart_container": "gui/sprites/hud/heart/container.png",
    "heart_full": "gui/sprites/hud/heart/full.png",
    "heart_half": "gui/sprites/hud/heart/half.png",
    "hunger_container": "gui/sprites/hud/food_empty.png",
    "hunger_full": "gui/sprites/hud/food_full.png",
    "hunger_half": "gui/sprites/hud/food_half.png",
    "hotbar": "gui/sprites/hud/hotbar.png",
    "hotbar_selection": "gui/sprites/hud/hotbar_selection.png",
    "hotbar_offhand_left": "gui/sprites/hud/hotbar_offhand_left.png",
    "item_diamond_sword": "item/diamond_sword.png",
    "item_diamond_pickaxe": "item/diamond_pickaxe.png",
    "item_golden_apple": "item/golden_apple.png",
    "item_bow": "item/bow.png",
    "item_bow_pulling_0": "item/bow_pulling_0.png",
    "item_bow_pulling_1": "item/bow_pulling_1.png",
    "item_bow_pulling_2": "item/bow_pulling_2.png",
    "item_elytra": "item/elytra.png",
    "item_totem_of_undying": "item/totem_of_undying.png",
    "heart_absorb_full": "gui/sprites/hud/heart/absorbing_full.png",
    "heart_absorb_half": "gui/sprites/hud/heart/absorbing_half.png",
    "particle_crit": "particle/critical_hit.png",
    "particle_damage": "particle/damage.png",
    **{f"particle_sweep_{i}": f"particle/sweep_{i}.png" for i in range(8)},
    "item_arrow": "item/arrow.png",
    "item_trident": "item/trident.png",
    "item_flint_and_steel": "item/flint_and_steel.png",
    "item_ender_pearl": "item/ender_pearl.png",
    "item_bread": "item/bread.png",
    "item_cooked_beef": "item/cooked_beef.png",
    "item_firework_rocket": "item/firework_rocket.png",
    "item_zombie_spawn_egg": "item/zombie_spawn_egg.png",
    "item_creeper_spawn_egg": "item/creeper_spawn_egg.png",
    "item_skeleton_spawn_egg": "item/skeleton_spawn_egg.png",
    "block_dirt": "block/dirt.png",
    "block_stone": "block/stone.png",
    "block_tnt_top": "block/tnt_top.png",
    "block_tnt_side": "block/tnt_side.png",
    "block_tnt_bottom": "block/tnt_bottom.png",
    "heart_container_blinking": "gui/sprites/hud/heart/container_blinking.png",
    "heart_full_blinking": "gui/sprites/hud/heart/full_blinking.png",
    "heart_half_blinking": "gui/sprites/hud/heart/half_blinking.png",
    "xp_bar_background": "gui/sprites/hud/experience_bar_background.png",
    "xp_bar_progress": "gui/sprites/hud/experience_bar_progress.png",
    **{f"destroy_stage_{i}": f"block/destroy_stage_{i}.png" for i in range(10)},
    # Mining loot icons (issue #81)
    "item_coal": "item/coal.png",
    "item_raw_copper": "item/raw_copper.png",
    "item_raw_iron": "item/raw_iron.png",
    "item_redstone": "item/redstone.png",
    "item_raw_gold": "item/raw_gold.png",
    "item_lapis_lazuli": "item/lapis_lazuli.png",
    "item_amethyst_shard": "item/amethyst_shard.png",
    "item_diamond": "item/diamond.png",
    "item_emerald": "item/emerald.png",
    "item_netherite_scrap": "item/netherite_scrap.png",
    "item_cobblestone": "block/cobblestone.png",
    # Pickaxe tiers (issue #81)
    "item_wooden_pickaxe": "item/wooden_pickaxe.png",
    "item_stone_pickaxe": "item/stone_pickaxe.png",
    "item_iron_pickaxe": "item/iron_pickaxe.png",
    "item_golden_pickaxe": "item/golden_pickaxe.png",
    "item_netherite_pickaxe": "item/netherite_pickaxe.png",
    "block_cobblestone": "block/cobblestone.png",
    "block_wood_top": "block/oak_log_top.png",
    "block_wood_side": "block/oak_log.png",
    "block_obsidian": "block/obsidian.png",
    # Crafting items (issue #67)
    "item_wooden_sword": "item/wooden_sword.png",
    "item_stone_sword": "item/stone_sword.png",
    "item_iron_sword": "item/iron_sword.png",
    "item_netherite_sword": "item/netherite_sword.png",
    "item_stick": "item/stick.png",
    "item_iron_ingot": "item/iron_ingot.png",
    "item_gold_ingot": "item/gold_ingot.png",
    "item_netherite_ingot": "item/netherite_ingot.png",
    "item_string": "item/string.png",
    "item_gunpowder": "item/gunpowder.png",
    "item_flint": "item/flint.png",
    "item_apple": "item/apple.png",
    # The crafting table's block faces
    "block_crafting_table_top": "block/crafting_table_top.png",
    "block_crafting_table_side": "block/crafting_table_side.png",
    "block_crafting_table_front": "block/crafting_table_front.png",
    # Buildable block faces
    "block_sand": "block/sand.png",
    "block_gravel": "block/gravel.png",
    "block_planks": "block/oak_planks.png",
    "block_stone_bricks": "block/stone_bricks.png",
    "block_bricks": "block/bricks.png",
    "block_white_wool": "block/white_wool.png",
    "block_anvil_top": "block/anvil_top.png",
    "block_anvil_side": "block/anvil.png",
    # Non-cube / transparent block faces (the item icons for glass/leaves/slab are isometric cubes above).
    "block_glass": "block/glass.png",
    "block_leaves": "block/oak_leaves.png",
    "block_stone_slab": "block/stone.png",
    "block_torch": "block/torch.png",
    "item_torch": "block/torch.png",
}


# The 3-row container background (gui/container/generic_54.png): its top part is 17 + 3 * 18 rows, its bottom part (the
# player's inventory) starts at row 126, as in Minecraft's ContainerScreen.
_CONTAINER_TEXTURE = "gui/container/generic_54.png"
_CONTAINER_PARTS = {"container_top": (0, 0, 176, 71), "container_bottom": (0, 126, 176, 222)}
# Spawn eggs are a grey egg and grey spots tinted per mob (the item model's tint): primary colour for the egg, secondary for the spots.
_SPAWN_EGG_COLOURS = {
    "item_zombie_spawn_egg": ((0x00, 0xAF, 0xAF), (0x79, 0x9C, 0x65)),
    "item_creeper_spawn_egg": ((0x0D, 0xA7, 0x00), (0x00, 0x00, 0x00)),
    "item_skeleton_spawn_egg": ((0xC1, 0xC1, 0xC1), (0x49, 0x49, 0x49)),
}
_GLINT = (130, 60, 220)  # the enchantment glint, flattened into a tint for the enchanted golden apple

# Block items are drawn by Minecraft as isometric cubes: (top, left/right sides)
_JAR_BLOCK_ITEMS = {
    "item_dirt": ("block/dirt.png", "block/dirt.png"),
    "item_stone": ("block/stone.png", "block/stone.png"),
    "item_tnt": ("block/tnt_top.png", "block/tnt_side.png"),
    "item_cobblestone": ("block/cobblestone.png", "block/cobblestone.png"),
    "item_wood": ("block/oak_log_top.png", "block/oak_log.png"),
    "item_obsidian": ("block/obsidian.png", "block/obsidian.png"),
    # Crafting items (issue #67)
    "item_crafting_table": ("block/crafting_table_top.png", "block/crafting_table_front.png"),
    "item_anvil": ("block/anvil_top.png", "block/anvil.png"),
    "item_wood_planks": ("block/oak_planks.png", "block/oak_planks.png"),
    "item_sand": ("block/sand.png", "block/sand.png"),
    # Buildable block item icons
    "item_gravel": ("block/gravel.png", "block/gravel.png"),
    "item_stone_bricks": ("block/stone_bricks.png", "block/stone_bricks.png"),
    "item_bricks": ("block/bricks.png", "block/bricks.png"),
    "item_white_wool": ("block/white_wool.png", "block/white_wool.png"),
    # Non-cube / transparent block item icons.
    "item_glass": ("block/glass.png", "block/glass.png"),
    "item_leaves": ("block/oak_leaves.png", "block/oak_leaves.png"),
    "item_stone_slab": ("block/stone.png", "block/stone.png"),
}
_CUBE_SHADE = {"top": 1.0, "left": 0.78, "right": 0.6}

_ITEM_KEYS = [
    "item_diamond_sword", "item_diamond_pickaxe", "item_dirt", "item_stone", "item_tnt",
    "item_golden_apple", "item_bow", "item_elytra", "item_totem_of_undying",
]

# Mob skins supported for extraction (name -> path inside the jar's textures/)
_MOB_SKINS = {
    "zombie": "entity/zombie/zombie.png",
    "creeper": "entity/creeper/creeper.png",
    "skeleton": "entity/skeleton/skeleton.png",
}

# (right limb block origin, left limb block origin) in the 64x64 player layout: leg, arm
_LIMB_BLOCKS = (((0, 16), (16, 48)), ((40, 16), (32, 48)))
