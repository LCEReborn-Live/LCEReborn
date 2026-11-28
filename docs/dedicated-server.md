# Dedicated server downloads

Release builds produce two dedicated-server downloads in addition to the client:

- `LCEReborn-Live-Server-Windows-VERSION.zip`
- `LCEReborn-Live-Server-Linux-VERSION.tar.gz`

They contain the existing `LCERebornServer` executable, a startup script, bundled
prepared game assets, a headless asset preparation tool, and dependency licenses. They omit the
launcher, Qt, Node.js, account sessions, game dumps, saved worlds and logs. The
server also remains in the full portable client package for compatibility.

## Game data

Run `start-server.cmd` (Windows) or `./start-server.sh` (Linux). Prepared game
data is included in `assets/`, so normal startup needs no download or import.
The following preparation tools are only for recovery or source-only builds.
If game data is
missing, it automatically downloads the base game and Update 688, verifies their
SHA-256 checksums, extracts them and imports the merged content into `assets/`.
Subsequent starts reuse that folder. Allow at least **10 GB of free disk space**
for downloads, unpacked dumps and prepared content. Interrupted downloads resume
when you run preparation again.

To prepare the data separately, without starting a world:

```bat
prepare-server.cmd
```

```sh
./prepare-server.sh
```

Windows downloads a checksum-pinned, private Python runtime into `tools/python/`.
It does not install Python globally or change PATH. Linux requires `python3`;
on Ubuntu/Debian, install it with `sudo apt install python3` if missing.

You can instead copy an existing prepared `assets` folder from the launcher
installation into the server folder. Bundled assets live beside the launcher
on both Windows and Linux (inside the AppImage for that distribution).
To import already extracted local game dumps without downloading:

```sh
./prepare-server.sh --base "/path/to/base" --update "/path/to/update688"
```

On Windows use `prepare-server.cmd` instead. Inputs must be extracted title
folders containing `code`, `content` and `meta`. The update must be version 688.
The tool uses the existing import/merge logic and refuses to replace an existing
output folder. Pass `--output` to select another new assets folder.

Archives are cached in `downloads/game-files/`; pass `--cache` to preparation to
use another cache directory. Existing or incomplete `assets/` directories are
never silently replaced. `--content-dir` on the startup script selects existing
prepared data elsewhere and skips automatic preparation; `--help` also skips it.

## Start

Windows: run `start-server.cmd`. The Microsoft Visual C++ x64 Redistributable
is required, as for the client. Linux: extract the tar.gz and run:

```sh
./start-server.sh
# Or point at prepared data stored elsewhere:
./start-server.sh --content-dir "/path/to/assets"
```

The Linux package is for x86-64 with glibc and libstdc++; its README reports the
minimum glibc version found in the built server. It needs no desktop session,
Qt, AppImage or FUSE. The startup scripts change to their own directory, so
relative paths work even when invoked from another working directory.

The first start creates `saves/server.properties`. Configure it and restart.
Worlds and operators stay in `saves`, and logs stay in `logs`; use `--save-dir`
and `--log-dir` to select other directories. Packages contain none of these
runtime directories, so extracting an update does not replace saved worlds.
Back them up before upgrading.

Players connect through **Add Server** using `HOST:PORT`. The default game port
is **TCP 25570**, configurable through `port=` in `server.properties`. Configure
your host's firewall and router for that selected game port. No server/network
configuration is changed by extracting these packages.

Type `help`, `save-all` or `stop` in the server console. Ctrl+C also saves and
stops the server. This executable hosts the game world; the LiveAPI and relay
services are managed separately and are not installed by this package.

## Packaging from source

```sh
cmake --build build/pc --target LCERebornServer
python tools/package_server.py --build build/pc
```

The packager does not require a built launcher or deployed Qt libraries. It
uses the same dependency sources and license information as the client package.
The Windows workflow and Linux build container both create this package from
the same revision as the client. Their existing release-artifact upload then
publishes the extra files automatically. No extra full client build is needed.

To add server downloads to an existing release using an already successful
Windows/Linux build, run **Publish dedicated server packages** and supply that
build's run ID and the existing release tag. The workflow checks that the game
code and dependency pins still match the build, preserves the compiled server
binaries, and adds only the two server assets. Existing client assets and the
release tag are retained. The server README identifies its actual build commit.
