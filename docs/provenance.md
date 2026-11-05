# Provenance

## Where the source comes from

LCEReborn's game code started as a matching decompilation of the Wii U build. The Wii U executable ships unstripped, so its symbols, type info and strings gave the source its names and layouts. The function bodies were written by AI agents reading the compiled code, then compiled and compared with the original until they matched. The Xbox 360 TU75 build was used as a second reference.

The source is derived from the original executable, but it isn't tool output. No file was emitted by a decompiler. Every file is written in the project's own style.

## What's never in the repo

- Game binaries, in any form
- Game assets: textures, models, sounds, fonts, UI movies, language files, world templates and saves
- Symbol tables, disassembly, decompiler output, or anything else generated from the binary

## Rights

The game, its code and its content belong to their owners. The license in [LICENSE](../LICENSE) covers only what this project adds, and grants no rights to Minecraft: Wii U Edition itself. Only build it from your own copy of the game.

## Third-party code

`source/vendor/` rebuilds libraries the game links against. If a maintained upstream ever exists for one of them, it should replace the copy here.

The Mii face library already has one: aboood40091's FFL decompilation. It's pulled in as the `source/nn/ffl` submodule, a fork with this project's changes as separate commits. None of its code is copied into this repo.

## Contributing

These rules apply to everyone, maintainers included.

You can use AI tools to write your pull request. But you submit it, so you own it.

- **Commit as yourself.** No AI listed as the author or co-author. Claude, Codex or whatever you used isn't responsible for this code. You are.
- **Know what you're submitting.** Read it, understand it, and be ready to explain or fix it in review.
- **Meet the same bar as everything else.** It has to follow [conventions.md](conventions.md), build, and be clean and maintainable. Poorly written or unmanageable code gets sent back, however it was written.

The usual rules still apply: no decompiler output, binaries or assets. Reading the original's compiled code to understand it is fine. Pull requests that break these rules will be closed.
