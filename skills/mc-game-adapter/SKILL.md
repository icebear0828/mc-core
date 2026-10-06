---
name: mc-game-adapter
description: >-
  Scaffold and implement game-specific adapter modules for mc-core in any proprietary game engine.
  TRIGGER when: user wants to adapt a new game to mc-core, scaffold a game adapter, integrate mc-core with GTA/Cyberpunk/Elden Ring/Resident Evil, or says "适配新游戏", "接入游戏 X", "为游戏 Y 制作适配器".
  DO NOT TRIGGER when: user only asks general questions about mc-core or wants to edit mc-core core math without touching game adapters.
allowed-tools:
  - "Read"
  - "Write"
  - "Edit"
  - "Bash"
  - "Glob"
  - "Grep"
---

# MC Game Adapter Architect

Scaffold, engineer, and verify game-specific adapter plugins for `mc-core` in proprietary game engines.

## Overview

This skill guides the end-to-end integration of `mc-core` into any game. It handles engine reconnaissance, asset cooking pipelines, C++ adapter scaffolding across the four core contracts (`IPhysics`, `IRender`, `ICombat`, `IInput`), and verification against the quality gates.

## When to Use

- User requests adapting a new proprietary game (e.g., GTA V, Cyberpunk 2077, Elden Ring, Resident Evil).
- User wants to scaffold a new adapter directory under `mc-core/adapters/<game>/`.
- User needs guidance on game-specific model cooking, player mesh hiding, or native raycasting.

## Important Rules

1. **NEVER modify `include/mc/` core interfaces** to suit a specific game. Adapt the game to the contracts, not the contracts to the game.
2. **ALWAYS read the engine matrix first**: Load `references/engine-matrix.md` before writing adapter code.
3. **NEVER bind Steve to realistic humanoid skeletons**. Always use the 12 rigid-box mount approach with native player mesh hiding.
4. **ALWAYS verify against `references/validation-checklist.md`** before declaring an adapter complete.

## Key Workflows

### Phase 1: Engine Reconnaissance & Hook Selection

**Goal:** Identify the host engine, mod loader, player hide mechanism, and native raycast/collision APIs.

1. Read the engine matrix:
   ```
   Read: references/engine-matrix.md
   ```
2. Determine:
   - Target Engine: RAGE, REDengine, RE Engine, Dantelion, or custom.
   - Mod Loader / Hook SDK: ScriptHookV, Cyber Engine Tweaks, REFramework, ModEngine2.
   - Native Player Mesh Hide Method: Engine entity visibility function or material alpha override.
   - Native Raycast Method: Line trace or probe API.

### Phase 2: Asset Cooking Pipeline

**Goal:** Extract and convert Minecraft models into game-native model formats.

1. Run the asset extractor to generate standard OBJ and PNG files:
   ```bash
   uv run tools/extract_mc_assets.py --out-dir assets/exported/
   ```
2. Guide the user on converting generated OBJs to the target engine format:
   - **GTA V**: Use Sollumz / OpenIV to convert to `.ydr`.
   - **Cyberpunk 2077**: Use WolvenKit to convert to `.mesh`.
   - **Resident Evil**: Use RE Mesh Tools to convert to `.mesh`.
   - **Elden Ring**: Use FLVER Editor to convert to `.flver`.

### Phase 3: Adapter Scaffolding

**Goal:** Scaffold a complete C++ adapter project implementing all four contracts.

1. Read the contracts specification:
   ```
   Read: references/contracts-spec.md
   ```
2. Create directory `adapters/<game_name>/` with subdirectories `include/` and `src/`.
3. Read the templates:
   ```
   Read: assets/adapter-header.template.hpp
   Read: assets/adapter-source.template.cpp
   Read: assets/plugin-entry.template.cpp
   Read: assets/CMakeLists.template.txt
   ```
4. Generate the game-specific adapter files replacing `{{GAME_NAME}}` and `{{GAME_NAME_LOWER}}`:
   - `adapters/<game_name>/include/<game_name>_adapter.hpp`
   - `adapters/<game_name>/src/<game_name>_adapter.cpp`
   - `adapters/<game_name>/src/plugin_entry.cpp`
   - `adapters/<game_name>/CMakeLists.txt`
5. Wire up native engine calls for the four interfaces:
   - `IPhysicsAdapter`: Native raycast and physical prop collider registration.
   - `IRenderAdapter`: Native player hide call and 12-part Steve transform sync.
   - `ICombatAdapter`: Native damage event or health reduction dispatch.
   - `IInputAdapter`: Held weapon query and camera matrix read.

### Phase 4: Validation & Quality Gate

**Goal:** Verify compilation and execute in-game validation checks.

1. Read the validation checklist:
   ```
   Read: references/validation-checklist.md
   ```
2. Verify:
   - CMake compiles the adapter with `-std=c++20` and links against `mc_core`.
   - Native player mesh is hidden while capsule remains physically active.
   - Placed blocks allow character and NPC walking/collision.
   - Mining crack stages (0..9) progress and destroy both mesh and collider.
3. Report checklist status to user.
