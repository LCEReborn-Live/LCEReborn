# Texture pack crash regression, 2026-11-17

Selecting Old Textures took the selected-pack branch in `Textures::readImage`,
which skipped the base game's `1_2_2/` directory for non-original, non-title-update
images. The first missing image was `Common/res/mob/cavespider.png`. Base packs
now keep the same path rules when selected as an optional pack. DLC pack paths
continue to use the DLC layout.

The supplied world-generation minidump matches the 1.2.3 release binary's
CodeView GUID `{9701D93A-6CAC-6AB4-4C4C-44205044422E}`, age 1. At RVA `0x407222`
the instruction reads through RBX, which is zero in the exception context.
Matching the instruction sequence to the local symbols identifies
`DataFixerUpper::updateData + 0x52`; the stack also matches `StructureManager::get`.
The fix in commit `1e1a2be5` supplies the game's fixer to the DLC structure
manager. The hotfix build includes that existing fix.

The launcher now includes both the decimal exit code and, on Windows, its
eight-digit hexadecimal representation when a game process crashes. This makes
early exits diagnosable even when there are no game log lines. The separate
startup failure shown in the video cannot be identified from the video alone.

## Validation

With a native Windows Ninja/clang-cl build and prepared game content:

```powershell
cmake --build build/game-ninja --target LCEReborn LCERebornLauncher
python tools/tests/run_texture_pack_regression.py --content-dir <prepared-assets>
```

The integration check uses isolated saves and logs. It mounts the bundled 1.14
pack, loads all 188 structure templates with both packs and two fresh managers
each (752 loads), switches packs four times with full indexed texture, atlas and
colour-map reloads, and compares Old Textures cave spider pixels with the base
game PNG. It requires the completion marker because `FatalLoadError` currently
exits with status zero.

All checks passed. Negative controls using the previous `Textures.cpp` reproduce
the exact missing cave spider error; using the pre-fix DLC structure manager
reproduces access violation `0xC0000005` in the structure-loading test.
