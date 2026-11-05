# Overview

LCEReborn is a cross-platform reimagining of Legacy Console Edition, built on the Wii U Edition decompilation at 1.13, the Aquatic Update. It runs on Windows, Linux and the Wii U (in Cemu), and all three can play together.

The decompilation it started from was written by AI agents. Work on LCEReborn itself follows the same rules for everyone: you can use AI tools, but you commit as yourself and you're responsible for the code. See [provenance.md](provenance.md) for where the source comes from and how to contribute.

## What's in the repo

- `source/game/`: the game itself
- `source/platform/`: the Wii U and PC platform layers
- `source/net/`: multiplayer shared by every platform
- `source/vendor/`, `source/nn/`, `source/cafe/`: library and SDK code the game needs
- `launcher/`: the PC launcher
- `tools/`: building and packaging

[architecture.md](architecture.md) goes into more detail.

## What's not in the repo

No game assets, saves, executables, compiled shaders, symbol tables, disassembly or decompiler output. None of that should ever be committed.

## Where things stand

The Wii U build runs in stock Cemu 2.6. It boots, creates and loads worlds, and holds a full 20 ticks per second. It hasn't been tried on real hardware.

The PC version plays on Windows and Linux, with worlds, mini games, split screen and multiplayer.
