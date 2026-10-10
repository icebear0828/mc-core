# Mod Packaging Quality Checklist

Verification checklist for mc-core mod distribution.

## Pre-Packaging Gates

- [ ] **Git Synchronization Gate**:
  - macOS and Windows machines share the exact same commit:
    `git rev-parse --short HEAD` matches on both sides.
- [ ] **Release Compilation Gate**:
  - Built with MSVC under `Release` configuration (`/W4 /WX /permissive- /utf-8`).
  - No compilation errors or warnings.
  - Target output is x64 and does not link against debug runtimes (`MSVCP140D.dll` / `VCRUNTIME140D.dll`).
- [ ] **Game State Gate**:
  - Target game process is closed before inspecting binaries or deploying:
    `tasklist | findstr /I "eldenring start_protected sekiro"` returns empty.

## Post-Packaging Gates

- [ ] **Archive Integrity Gate**:
  - The generated `.zip` can be opened and inspected without corruption:
    `tar -tf dist/<pkg_name>.zip`
- [ ] **File Checklist (Elden Ring Nexus Package)**:
  - `dinput8.dll` exists in root of archive.
  - `mc_er_hit.bin` exists and matches repository template (576 bytes).
  - `mc_er_steve.txt` contains valid default settings (`occlusion=1`, `blocks=1`, `mc_jump=0`, `fall_reset=0`).
  - `README_INSTALL.txt` has explicit EAC disabled and offline requirement warnings.
  - `setup_assets.bat` exists for one-click asset extraction by users.
  - `SHA256SUMS.txt` is populated and matches `certutil -hashfile` output.
- [ ] **Copyright Compliance Gate**:
  - Nexus distribution zip does not contain raw unpacked proprietary client sound packs without user extraction step.
