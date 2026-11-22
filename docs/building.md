# Building for Wii U

This builds `Minecraft.Client.rpx` and packages it as a homebrew bundle, `build/LCEReborn.wuhb`, that Cemu and Aroma run. For Windows and Linux, see "Building" in [pc-platform.md](pc-platform.md).

## What you need

- The repo with its submodule: clone with `--recurse-submodules`, or run `git submodule update --init`
- Your own decrypted dumps of the base game (`00050000101D9D00`) and update 688 (`0005000E101D9D00`)
- Python 3.8 or newer
- Docker, with about 15 GB free for the first build and at least 4 GB of memory
- Cemu 2.6 or newer, or a Wii U running Aroma, to run it

## Steps

```
python tools/prepare.py --base <base game dump> --update <update v688 dump>
python tools/build.py
python tools/package.py --update <update v688 dump>
python tools/launch_cemu.py <path to Cemu>
```

1. **`prepare.py`** pulls the shaders and game content out of your dumps into `source/platform/wiiu/shaders/` and `assets/`. Both are ignored by git.
2. **`build.py`** builds the RPX in Docker using [RedHills](https://github.com/Jacobwasbeast/RedHills), a clang fork that matches the original compiler's class layout. The first run takes about 45 minutes, mostly compiling RedHills. After that it's a few minutes.
3. **`package.py`** writes the bundle: the RPX as `code/root.rpx`, the menu icon and boot screens from your update dump's `meta/`, and `assets/` as `content/`. It's about 1.5 GB and holds your game files, so don't share it.

The bundle is a title of its own (`0005000F5C4F365C`) with its own saves, so it doesn't need the game installed in Cemu or on the console, and it won't see saves from the retail game. To run it on a Wii U, copy it to `wiiu/apps/` on the SD card. Packs go in `wiiu/LCEReborn/packs` on the same card.

On Windows, pass a quoted path such as `"C:\Games\Cemu\Cemu.exe"`. If Cemu is on `PATH`, omit the path. Close any running Cemu instance before starting the bundle this way. The launcher gives the new emulator process the verified [ISRG Root X1 certificate](https://letsencrypt.org/certificates/) used by LCE Live. This leaves TLS verification enabled and requires no changes to the system trust store.

On a Wii U with the native NSSL service, the game adds the same root certificate to its LCE Live network context. The regular certificate and hostname checks remain enabled.

## GitHub Actions

The **Wii U build** workflow runs on manual dispatch on a GitHub-hosted Linux runner. It verifies the base game and Update 688 archives against the game download manifest, generates the build inputs, and uses the Docker build above.

GitHub Actions caches the compiled RedHills toolchain by Dockerfile before compiling the game. A cache miss requires a full compiler build.

The `wiiu-wuhb-complete` artifact contains `LCEReborn-complete.wuhb` with the base game and Update 688 content built in. Extract the WUHB to `wiiu/apps/` on a Wii U SD card for Aroma, or launch it on a PC with `python tools/launch_cemu.py "<path to Cemu>" --bundle "<downloaded WUHB>"`.

The smaller `wiiu-wuhb` artifact contains `LCEReborn.wuhb` for an existing SD card layout. It uses prepared game files in `wiiu/LCEReborn/content/` on the Wii U SD card, or in the `sdcard/` folder under Cemu's user data directory.

Both bundles include Modern Textures. The game loads it automatically as the default pack, and classic Minecraft textures are still available in the pack selector.

The `wiiu-rpx` artifact contains `Minecraft.Client.rpx` and its link map. To combine the downloaded RPX and both of your dumps into a single WUHB, run:

```
python tools/package.py --base "<base dump>" --update "<Update 688 dump>" --rpx "<downloaded Minecraft.Client.rpx>" --output "build/LCEReborn-complete-local.wuhb"
```

Open that WUHB in Cemu with `tools/launch_cemu.py --bundle "build/LCEReborn-complete-local.wuhb"`, or copy it to `wiiu/apps/` on an Aroma SD card. The game content is already inside the bundle.

Supply an existing `release_tag` to attach the experimental WUHB and RPX to that release and add the usage instructions to its notes.

## Options for `build.py`

| Option | What it does |
| --- | --- |
| `--jobs N` | Number of parallel jobs. Lower it if you run out of memory. |
| `--keep-going` | Report every failure instead of stopping at the first. |
| `--clean` | Rebuild from scratch. |
| `--local` | Build without Docker, using RedHills, LLVM 22 and devkitPPC installed on your machine. |

To free the space, run `docker volume rm minecraft-wiiu-build` and remove the `minecraft-wiiu-redhills` image.
