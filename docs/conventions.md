# Code conventions

Everything under `source/` follows these rules so the tree reads like one codebase.

## Language

The rules depend on where the code lives:

| Where | Language |
| --- | --- |
| `source/game/` and other shared code | C++03 style. It has to build for the Wii U as well as PC. No `auto`, lambdas, range-for, `nullptr`, `override`, `constexpr` or rvalue references. |
| `source/platform/pc/` and `launcher/` | C++17. Use it where it makes the code clearer. |
| `source/platform/wiiu/` | Same as shared code. |

- Shared code uses boost 1.x for smart pointers, unordered containers and hashing, and the standard library for everything else.
- No compiler extensions or `#pragma pack`, except where the original used a processor instruction directly. That code goes behind a compiler check.

## Files

- One class family per header and `.cpp`, named after the outermost class: `Block.hpp` and `Block.cpp`.
- Nested and helper classes live in their outer class's files.
- Header-only families get no `.cpp`.

## Naming

- **Original classes keep their original names:** Mojang-style in the game core (`LivingEntity`), 4J's `C` prefix in the console layer (`CStorage`), and `FJ_` / `fui` in the UI.
- **New classes follow the style of the layer they're in.** In `source/platform/pc/`, that's the `Pc` prefix (`PcSoundEngine`, `PcWindow`).
- Members use `mName` in the game core and `m_name` (`m_p` for pointers) in the 4J layers.
- Anything else just needs to read naturally.

## Content

- **Comments explain why, not what.** Add one where the reason for something isn't obvious. Don't add notes about where code came from.
- **No stubs or placeholders.** A method has a real body or doesn't exist yet.
- **No machine-generated code.** No raw memory access in place of members, no hand-built vtables, no decompiler-style names.
- **Platform differences go behind `#if LCE_PLATFORM_WIIU` / `#elif LCE_PLATFORM_PC`.** Keep them small and leave the other platform's code alone.

## Layout

- Includes are relative to `source/`: the family's own header first, then project headers, then system headers.
- Prefer forward declarations in headers.
- Format with `source/.clang-format` (clang-format 22).
