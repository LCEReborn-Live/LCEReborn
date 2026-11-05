# PC launcher

`LCERebornLauncher` imports your game files, keeps your profiles and starts the game. It comes with every release.

## Setting up

The first time you open it, setup asks for:

1. **Base game:** your dump of the game
2. **Update 688:** your dump of the update
3. **Import:** copies the game into `assets/`

"Re-import game files" in the Profiles tab runs setup again.

## Your DLC

Packs you own don't load from the game files. Copy their folders from `content/WiiU/DLC` in your update dump into the packs folder. The launcher has a button that opens it.

## Profiles

Each profile has its own name, game folder and language, and can turn split screen and the join prompt on or off. Game Log shows the game's output, News shows releases, and an Update button appears when a new one is out.

## Where files go

- **Windows:** next to the launcher
- **Linux:** `~/.local/share/LCEReborn` (or `$XDG_DATA_HOME`)

## Building it

```
python tools/build/qt.py
```

installs Qt into `build/qt/`, and the PC build then builds the launcher too. Pass `-DLCE_BUILD_LAUNCHER=OFF` to skip it.

If you add release icons to `launcher/BlockIcons.cpp`, add them at the end and run `python tools/check_launcher_blocks.py`.
