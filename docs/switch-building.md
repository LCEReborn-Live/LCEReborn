# Experimental Nintendo Switch port

The `nintendo-switch-port` branch targets a homebrew build of LCEReborn for the first Nintendo Switch. The first milestone is the main menu and an offline world. The port shares the PC renderer, input mappings and save format. The Nintendo Switch workflow runs when this branch is pushed.

## Try a build

1. Download `LCEReborn-Full.nro` when available. Copy it to `switch/` on the SD card and open it in the Homebrew Menu. It contains the client, Modern Textures, the base game assets and Update 688.
2. With the regular `LCEReborn.nro`, prepare `assets/` using `python tools/prepare.py --base "<base dump>" --update "<Update 688 dump>"` or reuse your prepared PC launcher assets. Put `assets/` in `switch/LCEReborn/`. The regular NRO already contains the client resources and Modern Textures.
3. For an emulator, copy the NRO to its virtual SD card before opening the file. Game saves and startup logs are written to `switch/LCEReborn/saves/` and `switch/LCEReborn/logs/`.

Open **LCE Live** on the main menu and enter the displayed code at [lcereborn.xyz/authorize](https://lcereborn.xyz/authorize). The session is restored on the next launch. Use **LCE Live** for friends and invitations, and **Invite Friends** in the pause menu of a hosted online world.

On hardware, run homebrew in application mode so the game has enough memory. For this first milestone, create a small offline world and use a new save. The Switch port currently uses the Wii U save buffer limit.

## Prepare a complete SD-card ZIP

Use the prepared `assets/` folder from the PC launcher or from step 2 above. This ZIP remains an option for installations using external game data.

```sh
python tools/package_switch.py --client nintendo-switch-nro.zip --assets assets --output build/LCEReborn-Switch-SD.zip
```

Extract the resulting ZIP at the root of the SD card or emulator SD card. It contains `switch/LCEReborn/` with the client, the Modern Textures pack and the prepared base game and Update 688 files. Keep your personal archive alongside your other game backups.

GitHub Actions always uploads `LCEReborn.nro` with embedded client resources. To also generate `LCEReborn-Full.nro` on each branch build, set the repository secret `SWITCH_ASSETS_URL` to an HTTPS download URL for a ZIP you are authorized to distribute. Set the repository variable `SWITCH_ASSETS_SHA256` to that ZIP's SHA-256. The source ZIP must contain a top-level `assets/` folder prepared from the base game and Update 688.

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

To also build a single-file NRO from your prepared game data, configure the same build with `-DLCE_SWITCH_ASSETS_DIR=/path/to/assets` and build the target `LCEReborn_full_nro`. Copy `build/switch/LCEReborn-Full.nro` to the SD card.
