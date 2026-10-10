---
name: mod-packager
description: >-
  Builds, validates, and packages game mod distributions (Elden Ring, Sekiro, Wukong) for Nexus Mods or internal testing.
  TRIGGER when: user requests to package a mod, build a release zip, publish to Nexus Mods, or run mod packaging pipeline.
  DO NOT TRIGGER when: user only wants to run unit tests or compile a debug build without packaging.
allowed-tools:
  - "Bash"
  - "Read"
  - "Write"
  - "Grep"
---

# Mod Packager

Automate building, packaging, and verifying multi-game mc-core mod releases for Nexus Mods and internal testing.

## Overview

Coordinates the end-to-end packaging pipeline across macOS development machines and Windows compilation environments. Generates EULA-compliant Nexus distribution archives, Mod Engine 2 chain-loading setups, SHA256 checksums, and installation documentation.

## When to Use

- User asks to package Elden Ring, Sekiro, or Wukong mods for distribution.
- User wants to create a release zip for upload to Nexus Mods.
- User wants to verify that build artifacts and configuration files are ready for release.

## Important Rules

1. Always verify macOS and Windows git revisions match (`git rev-parse --short HEAD`) before packaging.
2. Build in MSVC Release mode (`/W4 /WX /permissive- /utf-8`) on the Windows build machine. Never package debug DLLs.
3. For Nexus Mods public releases, always use `--channel nexus` to omit raw Mojang proprietary client assets.
4. Stop and audit the archive before declaring completion. Verify DLL checksums match `certutil -hashfile`.
5. For Elden Ring, ensure `mc_er_hit.bin` (576 bytes) is included in the package root.

## Key Workflows

### Phase 1: Environment & Sync Gate

**Goal:** Ensure clean git status and synchronize Windows remote repository.

1. Check local branch and commit:
   ```bash
   git status -s && git rev-parse --short HEAD
   ```

2. Push current branch if there are pending commits:
   ```bash
   git push origin <branch>
   ```

3. Synchronize the Windows target machine (`D:\game\mc\mc-core`):
   ```bash
   ssh win "cd /d D:\game\mc\mc-core && git -c http.proxy=http://127.0.0.1:7897 pull && git rev-parse --short HEAD"
   ```
   *Gate:* Do not proceed if commit hashes differ.

4. Verify target game is closed:
   ```bash
   ssh win "tasklist | findstr /I \"eldenring start_protected sekiro\""
   ```
   *Gate:* Do not proceed if game process is still active.

### Phase 2: Remote Release Compilation

**Goal:** Produce optimized Release DLL without compilation warnings or errors.

1. Consult [package-rules.md](file://.claude/skills/mod-packager/references/package-rules.md) for target names.

2. Compile release target on Windows:
   - For **Elden Ring**:
     ```bash
     ssh win "cd /d D:\game\mc\mc-core && call \"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 > build-env.log 2>&1 && cmake --build build-win --config Release --target eldenring_adapter > build-er.log 2>&1 & findstr /C:\"error\" /C:\"warning\" /C:\"eldenring_adapter.vcxproj ->\" build-er.log"
     ```
   - For **Sekiro**:
     ```bash
     ssh win "cd /d D:\game\mc\mc-core && cmake --build build-win --config Release --target sekiro_adapter > build-sekiro.log 2>&1 & findstr /C:\"error\" /C:\"warning\" /C:\"sekiro_adapter.vcxproj ->\" build-sekiro.log"
     ```

### Phase 3: Package Assembly

**Goal:** Assemble game folder tree, templates, configs, setup scripts, and create ZIP archive.

1. Execute `tools/package_mod.py` on Windows machine:
   - For **Elden Ring Nexus Package**:
     ```bash
     ssh win "cd /d D:\game\mc\mc-core && uv run --python C:\Python313\python.exe python tools\package_mod.py --game eldenring --build-dir build-win\bin\eldenring --version <version> --channel nexus --out-dir dist"
     ```
   - For **Sekiro Nexus Package**:
     ```bash
     ssh win "cd /d D:\game\mc\mc-core && uv run --python C:\Python313\python.exe python tools\package_mod.py --game sekiro --build-dir build-win\bin\Release --version <version> --channel nexus --out-dir dist"
     ```
   - For **Standalone Community Assets Pack** (for Cloud Drive/Community sharing):
     ```bash
     ssh win "cd /d D:\game\mc\mc-core && uv run --python C:\Python313\python.exe python tools\package_mod.py --package-assets --assets-dir \"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mods\mc_adapter\" --version <version> --out-dir dist"
     ```

### Phase 4: Verification & Audit Gate

**Goal:** Verify archive structure and validate checksums against build artifacts.

1. Inspect generated ZIP archive contents:
   ```bash
   ssh win "cd /d D:\game\mc\mc-core\dist && tar -tf mc_<game>_v<version>_<channel>.zip"
   ```

2. Validate against [checklist.md](file://.claude/skills/mod-packager/references/checklist.md).

3. Verify hash consistency:
   - Print package checksum file:
     ```bash
     ssh win "type D:\game\mc\mc-core\dist\mc_<game>_v<version>_<channel>\SHA256SUMS.txt"
     ```
   - Compare with original DLL hash:
     ```bash
     ssh win "certutil -hashfile <dll_path> SHA256"
     ```

4. Output delivery report including package path, archive size, and SHA256.

## Common Pitfalls

- Do not package debug builds (`Debug/dinput8.dll`); players will crash due to missing MSVC debug runtime libraries.
- Do not commit or package extracted Mojang WAV audio files directly in public Nexus releases.
- Do not forget `mc_er_hit.bin` in Elden Ring packages; without it, player attack pipeline will fail.
- Do not proceed if `git pull` on Windows was disconnected; ensure proxy `-c http.proxy=http://127.0.0.1:7897` is specified.
