# PC version

The PC version runs natively on Windows and Linux. It doesn't emulate anything. Most people start it from the launcher ([pc-launcher.md](pc-launcher.md)).

## Files and folders

By default the game reads `assets/`, `packs/` and `resources/`, and writes to `saves/`, all relative to where it runs. You can change these with `--content-dir`, `--packs-dir`, `--resource-dir` and `--save-dir`.

Other launch options:

| Option | What it does |
| --- | --- |
| `--username` | Player name |
| `--language` | Game language |
| `--uuid` | Player identity |
| `--world <name>` | Load a world from `saves/games/` straight away |
| `--join <address>` | Join a server straight away |
| `--help` | List every option |

## Multiplayer

PC and Wii U players can play together.

- **Hosting:** online games use port 25570 by default. Press X (Y on Wii U) on the Create or Load screen to open Online Settings.
- **Open to LAN:** opens a world you started offline, from the pause menu.
- **Joining:** the Join tab lists games on your network. Add Server saves any other address.

## Dedicated server

`LCERebornServer` comes in the same package and runs a world with no window. It needs the content the launcher imported.

- **Windows:** run it from the package folder.
- **Linux:** run it from the tar.gz, not the AppImage, with `--content-dir ~/.local/share/LCEReborn/assets`.

The first start writes `server.properties` to the saves folder. Edit it and restart to apply changes. Useful settings:

- `world`: which world folder to load (it's created if missing)
- `motd`: the message shown in the server list (Java-style `&` colour codes work)
- `texture-pack`: a pack to send to players
- `autosave-minutes`: how often to save

Put a 64x64 `server-icon.png` next to it for a custom icon. Type commands into the console without the `/`, plus `save-all` and `stop`. Ctrl+C saves and stops too.

## Packs

The packs that came free with the game load automatically. Anything else goes in the packs folder as a `.pck` file, or as a folder holding one. On Wii U that's `wiiu/LCEReborn/packs` on the SD card. Packs on the main menu shows what's loaded.

If a server uses a pack you don't have, it'll offer to send it to you. Downloaded packs are cached, so you won't be asked twice.

## Shader packs

OptiFine and Iris shader packs go in `shaderpacks/` in the save folder, as `.zip` files or folders. Pick one in Settings > More Options > Shaders. Shaders only work with one player on screen.

## Video settings

Settings > More Options has the settings the console never let you change:

- Render distance, up to 32 chunks
- Simulation distance
- FOV, from 30 to 110
- Fancy graphics, smooth lighting, particles and biome blend

## Chat and commands

Messages can use `&` colour codes. Commands follow Java 1.12, with a few from 1.13, and include `/give`, `/tp`, `/gamemode`, `/time`, `/weather`, `/fill`, `/execute` and more. `/help` lists what you can use. Selectors like `@p`, `@a` and `@e[type=zombie,r=10]` work too.

## Logs and crashes

Logs go to `logs/latest.log`. If the game crashes, a report is written to `crash-reports/`. Please include it when you report a bug.

## Building

```
python tools/build/deps.py build/deps
python tools/build/layout.py build/deps build/third_party
python tools/build/qt.py
cmake -S . -B build/pc -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
cmake --build build/pc
```

- **Windows:** run this from a Visual Studio x64 developer prompt.
- **Linux:** leave out the compiler options to use your system's clang or GCC. You'll need CMake 3.24+, Ninja and SDL's development packages (`tools/build/Dockerfile.linux` lists them).

`python tools/build_linux.py` builds the Linux version in Docker from any system. `python tools/package_pc.py` packages a build for players, and `python tools/release_pc.py` publishes it as a GitHub release.
