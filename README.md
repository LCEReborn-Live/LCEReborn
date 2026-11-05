# LCEReborn

LCEReborn is a cross-platform reimagining of Legacy Console Edition, built on the Wii U Edition decompilation at 1.13, the Aquatic Update.

It runs on Windows, Linux and the Wii U (via Cemu), and they can all play together. There are no game files in this repo, so you'll need your own copy of the game, dumped from your own console.

## Playing

1. Download the package for your system from the [latest release](https://github.com/LCEReborn/LCEReborn/releases/latest).
   - **Windows:** extract the zip anywhere. You'll need the Microsoft Visual C++ Redistributable, which most PCs already have.
   - **Linux:** make the AppImage executable and run it, or extract the tar.gz.
2. Run `LCERebornLauncher` and point it at your dumps of the base game and update 688.
3. Pick a profile and hit Play.

The full game is unlocked from the start, along with the packs that came free with it. To use DLC you own, copy its folders from `content/WiiU/DLC` in your update dump into the packs folder. The launcher has a button that opens it.

## What's new

- Keyboard and mouse or controllers, split screen, any window size or borderless fullscreen
- Render distance up to 32 chunks and larger world sizes
- OptiFine and Iris shader packs
- Open to LAN, a server list, and a dedicated server (`LCERebornServer`) for Windows and Linux
- Servers can send players the texture and map packs they're missing

## Building

```
git clone --recurse-submodules https://github.com/LCEReborn/LCEReborn.git
```

See [docs/building.md](docs/building.md) for the Wii U build and [docs/pc-platform.md](docs/pc-platform.md) for Windows and Linux.

## Credits

LCEReborn is built on [mcwiiu-reconstructed](https://github.com/Jacobwasbeast/mcwiiu-reconstructed), the matching decompilation of Minecraft: Wii U Edition. That source was written by AI agents and checked function by function against the original executable. See [docs/provenance.md](docs/provenance.md) for how it was made.

## Disclaimers

- This is an unofficial fan project, not affiliated with or endorsed by Mojang, Microsoft, 4J Studios or Nintendo. Minecraft is a trademark of Mojang Synergies AB, and Wii U is a trademark of Nintendo.
- Back up your saves. A bug can still corrupt a world.
- The Wii U build has only been tested in Cemu. Real hardware is at your own risk.

## License

LCEReborn is licensed under the [GNU GPL v3 or later](LICENSE). That covers this project's code only, not Minecraft or its assets. Third-party code in the repo keeps its own license.
