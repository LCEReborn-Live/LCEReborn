# Architecture

The game is one codebase with a platform layer underneath. Only the platform layer talks to the OS: Cafe OS and GX2 on Wii U, SDL 3 and OpenGL on PC. Game code reaches it through shared interfaces, and only checks which platform it's on where the behaviour actually differs, like mouse look on PC.

## Layout

| Folder | What's in it |
| --- | --- |
| `source/game/` | The game: world, blocks, entities, items, networking, UI, rendering and audio |
| `source/platform/wiiu/` | The Wii U platform layer |
| `source/platform/pc/` | The PC platform layer: SDL 3, OpenGL 3.3, input, sound, logging |
| `source/net/` | Multiplayer shared by every platform (TCP and UDP on port 25570) |
| `source/vendor/` | Library internals the game links against, like Miles, Bink audio and dlmalloc |
| `source/nn/` | Small Nintendo SDK libraries, plus the `ffl` Mii submodule |
| `source/cafe/` | Cafe SDK pieces: GX2 declarations, maths and runtime support |

Inside `source/game/`, things are grouped how you'd expect: `world/`, `entity/`, `item/`, `network/`, `server/`, `command/`, `minigame/`, `ui/`, `client/render/` and so on.

## Includes

Every include is relative to `source/`, like `#include "game/world/block/Block.hpp"`. Game code includes the shared headers in `source/platform/`, never `platform/wiiu/` or `platform/pc/` directly.

## Platform code

Code that differs per platform uses `#if LCE_PLATFORM_WIIU` / `#elif LCE_PLATFORM_PC`. Each build defines exactly one.

The rule: if a function ends up calling the OS, it belongs in the platform layer. Game code that calls the platform directly is a bug.

## The Mii submodule

`source/nn/ffl` points at the `wiiu-recon` branch of https://github.com/Jacobwasbeast/ffl, a fork of aboood40091's FFL decompilation brought up to version 1.5.6.
