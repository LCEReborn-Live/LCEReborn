# Building for Wii U

This builds `Minecraft.Client.rpx` and packages it so Cemu can run it. For Windows and Linux, see "Building" in [pc-platform.md](pc-platform.md).

## What you need

- The repo with its submodule: clone with `--recurse-submodules`, or run `git submodule update --init`
- Your own decrypted dumps of the base game (`00050000101D9D00`) and update 688 (`0005000E101D9D00`)
- Python 3.8 or newer
- Docker, with about 15 GB free for the first build and at least 4 GB of memory
- Cemu 2.6 or newer, with the base game in its game list

## Steps

```
python tools/prepare.py --base <base game dump> --update <update v688 dump>
python tools/build.py
python tools/package.py --update <update v688 dump>
Cemu -g build/title/code/Minecraft.Client.rpx
```

1. **`prepare.py`** pulls the shaders and game content out of your dumps into `source/platform/wiiu/shaders/` and `assets/`. Both are ignored by git.
2. **`build.py`** builds the RPX in Docker using [RedHills](https://github.com/Jacobwasbeast/RedHills), a clang fork that matches the original compiler's class layout. The first run takes about 45 minutes, mostly compiling RedHills. After that it's a few minutes.
3. **`package.py`** lays out `build/title/` the way Cemu expects. Your installed game isn't touched.

## Options for `build.py`

| Option | What it does |
| --- | --- |
| `--jobs N` | Number of parallel jobs. Lower it if you run out of memory. |
| `--keep-going` | Report every failure instead of stopping at the first. |
| `--clean` | Rebuild from scratch. |
| `--local` | Build without Docker, using RedHills, LLVM 22 and devkitPPC installed on your machine. |

To free the space, run `docker volume rm minecraft-wiiu-build` and remove the `minecraft-wiiu-redhills` image.
