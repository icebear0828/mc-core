# Packaging Standard

How Elden Ring builds are packaged for release. Follow this when adding a mode,
an asset family, or a new packaging channel. The goal is one predictable layout
and one set of names across local builds and CI.

## 1. Variants and build axes

Three independent CMake options select what is compiled into `dinput8.dll`:

| Option | Meaning |
|---|---|
| `ENABLE_MC_MODE` | Minecraft gameplay |
| `ENABLE_CS2_VIEWMODEL` | Counter-Strike 2 arsenal and viewmodel |
| `ENABLE_OW_VIEWMODEL` | Overwatch Doomfist viewmodel |

A release is exactly one combination. The four supported variants are:

| Variant | CMake flags | Mode key |
|---|---|---|
| Minecraft | MC=ON, CS=OFF, OW=OFF | `mc` |
| Counter-Strike | MC=OFF, CS=ON, OW=OFF | `cs` |
| Overwatch | MC=OFF, CS=OFF, OW=ON | `ow` |
| Combined | all three ON | `mc_cs_ow` |

`all` is the CLI alias for `mc,cs,ow`.

Package name, always:

```
mc_eldenring_<mode_key>_v<version>_<channel>.zip
```

Examples: `mc_eldenring_mc_v0.1.0_nexus.zip`,
`mc_eldenring_mc_cs_ow_v0.1.0_nexus.zip`.

## 2. Package layout

A package is a **game-root overlay**: extract it into the folder containing
`eldenring.exe`.

```
dinput8.dll
mc_er_mode.txt
mc_er_*.txt                 # mode configs (MC and/or OW)
mods/mc_adapter/
  mc_hud_atlas.png          # MC
  steve.png                 # MC
  models/*.geo.json         # MC
  sounds/...                # MC, CS or OW by ownership
  textures/...              # MC, CS or OW by ownership
  cs_*_viewmodel.bin        # CS
  ow_*                      # OW
README_INSTALL.txt
SHA256SUMS.txt
```

`mc_er_mode.txt` is read at launch and must exist in every mode package:

```
# Written by tools/package_mod.py: the mode the game starts in.
start_mode=<mc|cs|ow>
available_modes=<comma-separated list>
```

Hotkeys still switch between every mode compiled into the DLL; `start_mode`
only picks the launch mode.

## 3. Asset ownership

`mods/mc_adapter/mode_manifest.json`, written by
`tools/deploy_modes.py`, is the source of truth for CS and OW ownership.
Everything else under `mods/mc_adapter` belongs to MC.

- **CS**: `cs_*`, `textures/{ak47,awp,m4a1_s,knife_butterfly,glove_hardknuckle,sleeves,cs}/**`,
  the CS sounds, and `sounds/sounds_manifest.txt`.
- **OW**: `ow_*`, `mc_er_ow.txt`, `sounds/ow/**`.
- **MC**: the atlas, Steve/mob skins, `models/**`, `textures/armor/**`, the MC
  sounds, and the root `mc_er_*.txt` configs.

Never package:

- `.bak*`, `backup*`, `mode_manifest.json`, `dinput8*`, `inventory.txt`,
  `steve.bbmodel*`;
- debug/log toggles such as `mc_er_dbg_anim.txt`, `mc_er_physlog*.txt`,
  `mc_er_perfstats.txt`.

Game-derived assets are never committed. They come from the local game install
or a private asset bundle.

## 4. Local packaging (本机提取打包)

Prerequisites: the game is closed, the assets are extracted locally, and
`tools/deploy_modes.py` is available in the universal-modder workspace.

```powershell
cd D:\code\universal-modder

# 1. Extract and deploy all three modes into the game directory.
uv run python tools/deploy_modes.py --modes all --deploy --yes

# 2. Split the installed assets by the mode manifest and build four packages.
cd mc-core
uv run python tools/package_modes_local.py --out-dir dist_local
```

Outputs:

```
dist_local/mc_eldenring_mc_v<version>_nexus.zip
dist_local/mc_eldenring_cs_v<version>_nexus.zip
dist_local/mc_eldenring_ow_v<version>_nexus.zip
dist_local/mc_eldenring_mc_cs_ow_v<version>_nexus.zip
```

To restore a preferred game-directory state afterwards:

```powershell
cd D:\code\universal-modder
uv run python tools/deploy_modes.py --modes mc --deploy --yes
```

`tools/package_modes_local.py` only reads the game directory. It never copies
assets into the repository.

## 5. CI packaging

`.github/workflows/release.yml` builds the four variants as a matrix and keeps
the Sekiro/Wukong packages. Each Elden Ring job:

1. configures `build-<mode>` with the flags from section 1;
2. builds `eldenring_adapter`;
3. optionally downloads a private asset bundle;
4. runs `tools/package_mod.py --modes <mode>`;
5. uploads `dist/*.zip`.

A `package-tests` job runs the packaging tests first. The `release` job
downloads every artifact, writes `SHA256SUMS_ALL.txt`, and creates the GitHub
release.

### Private asset bundle (optional)

Set repository secrets:

- `MC_ASSET_PACK_URL`: URL of a zip that mirrors the game root.
- `MC_ASSET_PACK_TOKEN`: optional bearer token for a private source.

The zip is expanded to `asset_bundle/` and passed as
`package_mod.py --assets-dir asset_bundle`. It is copied last, so authentic
textures, sounds and viewmodels replace the EULA-safe placeholders. Without the
secret, packages stay placeholder-only.

Example bundle layout:

```
mc_hud_atlas.png
mods/mc_adapter/steve.png
mods/mc_adapter/sounds/...
mods/mc_adapter/textures/cs/...
mods/mc_adapter/cs_butterfly_viewmodel.bin
mods/mc_adapter/ow_gauntlet.bin
mods/mc_adapter/ow_gauntlet.png
```

## 6. Adding a new mode

1. Add the CMake option in the root `CMakeLists.txt` and the adapter
   `CMakeLists.txt`.
2. Add it to `CANONICAL_MODES` / `CMAKE_OPTIONS` in
   `tools/deploy_modes.py` (universal-modder workspace) and
   `tools/package_mod.py` (this repo).
3. Add its asset ownership rules to `tools/deploy_modes.py` and
   `tools/package_modes_local.py`.
4. Add a matrix entry in `.github/workflows/release.yml`.
5. Add `mode_configs` / `mode_notes` entries to
   `package_specs/eldenring.json` if the mode needs defaults.
6. Add tests for parsing, naming, contents and isolation.
7. Keep the package name and layout from sections 1 and 2 unchanged.

## 7. Validation gates

Run before pushing a packaging change:

```powershell
uv run --with pytest pytest -q tests/test_package_mod.py tests/test_package_modes_local.py tests/test_package_eldenring_mod.py
```

For each variant:

- the DLL inside the zip must hash-match
  `build_modes/<combo>/bin/eldenring/dinput8.dll`;
- `mc_er_mode.txt` must contain the expected `start_mode` and
  `available_modes`;
- the zip must not contain another mode's assets or runtime junk;
- `SHA256SUMS.txt` must list every packaged file.

The C++ gate remains: `mc_tests` all green and `eldenring_adapter`
RelWithDebInfo `/W4 /WX` clean.

## 8. EULA and safety

- Never commit Mojang, CS2 or Overwatch assets.
- Use the local game install or a private asset bundle.
- The package README must keep the offline / EAC-disabled warning.
- Do not publish a package that contains extracted game assets without the
  rights to distribute them.
