# Mod Packaging Rules & Specifications

Reference documentation for building, loading, and publishing mc-core adapters.

## Supported Games and Targets

| Game | ID | Build Target | Output Artifact | Loader Type | Notes |
|---|---|---|---|---|---|
| Elden Ring | `eldenring` | `eldenring_adapter` | `bin/eldenring/dinput8.dll` | DirectInput8 Proxy / ME2 | Must be offline, EAC disabled |
| Sekiro | `sekiro` | `sekiro_adapter` | `bin/Release/sekiro_adapter.dll` (alias `dinput8.dll`) | DirectInput8 Proxy / ME2 | Supports Mod Engine chain loading |
| Wukong | `wukong` | `wukong_adapter` | `bin/Release/wukong_adapter.dll` | UE4SS / Custom Loader | Unreal Engine 5 integration |

## Mod Engine 2 (ME2) Compatibility Profiles

When a user uses Mod Engine 2 instead of direct root DLL proxy:

1. **Elden Ring**:
   - Rename `dinput8.dll` to `mc_eldenring_adapter.dll`.
   - Place in `modengine2/` or game folder.
   - Add to `config_eldenring.toml`:
     ```toml
     external_dlls = ["mc_eldenring_adapter.dll"]
     ```

2. **Sekiro**:
   - Keep `sekiro_adapter.dll` under `mods/mc_adapter/`.
   - Configure `modengine.ini`:
     ```ini
     chainDInput8DLLPath = "mods\\mc_adapter\\sekiro_adapter.dll"
     ```

## Distribution Channels

1. **`nexus` Channel (Public / EULA Compliant)**:
   - Contains: Code DLL (`dinput8.dll`), hit template (`mc_er_hit.bin`), default config files, procedural placeholder HUD atlas, `setup_assets.bat`, `README_INSTALL.txt`, `SHA256SUMS.txt`.
   - Omits: Direct Mojang copyrighted client assets (Steve skin, 90 WAV sound files).
   - Allows users to generate high-res textures/sounds locally via `setup_assets.bat` using their own `client.jar`.

2. **`full` Channel (Internal / Full Preview)**:
   - Contains: All nexus files plus pre-extracted official high-res Steve skin, 512x512 HUD atlas, and full audio directory (`mods/mc_adapter/sounds/`).
   - For private testing and verification only.
