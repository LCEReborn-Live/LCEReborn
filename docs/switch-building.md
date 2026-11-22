# Experimental Nintendo Switch port

The `nintendo-switch-port` branch targets a homebrew build of LCEReborn for the first Nintendo Switch. The first milestone is the main menu and an offline world. The port shares the PC renderer, input mappings and save format. The Nintendo Switch workflow runs when this branch is pushed.

## Try a build

1. Download the `nintendo-switch-nro` artifact from the Nintendo Switch build workflow.
2. Put the extracted `LCEReborn` directory in `switch/` on the SD card or on the emulator's virtual SD card.
3. Run `python tools/prepare.py --base "<base dump>" --update "<Update 688 dump>"` with your own game dumps. Copy the resulting `assets/` into `switch/LCEReborn/` so the game finds `switch/LCEReborn/assets/Common/Media/MediaWiiU.arc`.
4. Open `LCEReborn.nro` in the homebrew menu or load it in Eden. Game saves and startup logs are written to `switch/LCEReborn/saves/` and `switch/LCEReborn/logs/`.

Keep the artifact's `packs/` folder next to `LCEReborn.nro`. Modern Textures loads automatically as the default pack.

Open **LCE Live** on the main menu and enter the displayed code at [lcereborn.xyz/authorize](https://lcereborn.xyz/authorize). The session is restored on the next launch. Use **LCE Live** for friends and invitations, and **Invite Friends** in the pause menu of a hosted online world.

On hardware, run homebrew in application mode so the game has enough memory. For this first milestone, create a small offline world and use a new save. The Switch port currently uses the Wii U save buffer limit.

## Prepare a complete SD-card ZIP

Use the prepared `assets/` folder from the PC launcher or from step 3 above.

```sh
python tools/package_switch.py --client nintendo-switch-nro.zip --assets assets --output build/LCEReborn-Switch-SD.zip
```

Extract the resulting ZIP at the root of the SD card or emulator SD card. It contains `switch/LCEReborn/` with the client, the Modern Textures pack and the prepared base game and Update 688 files. Keep your personal archive alongside your other game backups.

GitHub Actions always uploads a standalone `LCEReborn.nro` and the `nintendo-switch-nro` artifact with the client resources. To also generate `LCEReborn-Switch-SD.zip` on each branch build, set the repository secret `SWITCH_ASSETS_URL` to an HTTPS download URL for a ZIP you are authorized to distribute. Set the repository variable `SWITCH_ASSETS_SHA256` to that ZIP's SHA-256. The source ZIP must contain a top-level `assets/` folder prepared from the base game and Update 688.

## Compile using devkitPro

Install devkitA64, libnx, switch-cmake, switch-tools, switch-mesa, switch-curl and the host tools CMake 3.24+, Ninja and Python 3. Then run:

```sh
python3 tools/build/deps.py build/deps
python3 tools/build/layout.py build/deps build/third_party
cmake -S . -B build/switch -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DLCE_BUILD_LAUNCHER=OFF
cmake --build build/switch --target LCEReborn_nro
```

Copy `build/switch/LCEReborn.nro` to the game directory. Copy the contents of `resources/pc/` into its `resources/` subdirectory and the repository's `packs/` directory alongside it for Modern Textures.
