# Dedicated server downloads

Release builds produce two dedicated-server downloads in addition to the client:

- `LCEReborn-Live-Server-Windows-VERSION.zip`
- `LCEReborn-Live-Server-Linux-VERSION.tar.gz`

They contain the existing `LCERebornServer` executable, a startup script, bundled
packs, a headless asset preparation tool, and dependency licenses. They omit the
launcher, Qt, Node.js, account sessions, game dumps, saved worlds and logs. The
server also remains in the full portable client package for compatibility.

## Game data

Copy an existing prepared `assets` folder from the launcher installation into
the server folder. On Linux, launcher data normally lives in
`~/.local/share/LCEReborn/assets`; on Windows, it lives beside the installed
launcher. Alternatively, prepare the data on a headless host with Python 3:

```sh
python3 tools/prepare_server.py --base "/path/to/base" --update "/path/to/update688"
```

On Windows use `python` instead of `python3`. Inputs must be extracted title
folders containing `code`, `content` and `meta`. The update must be version 688.
The tool uses the existing import/merge logic and refuses to replace an existing
output folder. Pass `--output` to select another new assets folder.

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
