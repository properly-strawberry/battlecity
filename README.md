# Battle City (NES) — a verified C reconstruction

A reverse-engineering project that turns the program of *Battle City*
(Namco, 1985, Famicom/NES, mapper 0 / NROM-128) into readable,
**test-verified** C. "Verified" means the reconstruction produces the same
RAM, the same PPU/APU register traffic and the same controller handling as
the original, frame by frame, for the same inputs. That is checked by
differential tests against the original 6502 code, not taken on faith.

The project also includes a small SDL3 front end. It runs the reconstructed
game natively on Linux and Windows.

> **No game data is included.** The shared sources contain no ROM, no
> graphics, no music and no disassembly listing of the original program.
> Every tool and the game itself load **your own** ROM at run time, checked
> against the hashes below (see [Where to put the ROM](#where-to-put-the-rom)).
> Data tables (levels, sprites, sound scripts) are *named views*
> (address, length) into the loaded ROM. They are never copied into the source.
> Files that tools generate *from* the ROM (the full listing, the raw
> PRG/CHR split, screenshots, audio dumps, save states) stay local and are
> listed in `.gitignore`.

*Battle City* is © Namco (Bandai Namco Entertainment). This is an
unofficial, non-commercial research project and is not affiliated with
or endorsed by the rights holder.

## What's here

```
README.md            this file
CLAUDE.md            working notes / conventions for AI coding agents
docs/                notes from an earlier decompilation attempt (game rules,
                     memory map, subroutines); useful hints, not verified
movies/              FCEUX input recordings (.fm2) of real play sessions
re/
  Makefile           build + verification entry points
  src/               THE RECONSTRUCTION
    lifted/*.c       all 207 original routines as hand-written C, grouped in
                     9 subsystems (flow, flow_hud, screens, tanks, bullets,
                     cells, sprites, gfx_text, sound); *.lifted list which
                     original addresses each file replaces
    ram.h            one global RAM layout that mirrors the original addresses
                     (generated from tools/symbols.py)
    tables.h         named views into PRG[] for the 32 identified data tables
    rom.c / rom.h    loads and hash-checks the user's ROM
    machine.[ch]     PRG[]/RAM and the thin hardware layer (HAL)
    mech/            generated dispatch stubs (address -> C function) used by
                     the test harnesses
    platform/        SDL3 front end (main_sdl.c) and an own APU (apu.c)
  tools/
    emu/             reference 6502 + NROM emulator used as the test oracle
    harness/         lockstep, difftest, apucheck, audiocmp (C)
    disasm.py, disasm_cfg.py, lift.py, asm_check.py, m6502.py
                     disassembler, mechanical lifter, reassembler (all read
                     the ROM at run time)
    symbols.py       every named RAM address and routine, with its evidence
    difftest.py, verify_all.sh, check_game.sh, check_audio.sh
                     test drivers
    rom.py           ROM loader for the Python tools (+ --extract for Ghidra)
  docs/
    verification.md  what is verified, how, and the known differences
    ram_map.md       RAM map (generated) with how each address is known
    tables.md        data table catalogue (generated)
    subsystems/      per-subsystem notes on the lifted routines
    contracts.json   register in/out contracts per routine
    codemap.json     code/data classification of every PRG byte
    lifting_guide.md how routines are lifted to C
  evidence/          JSON results of the per-routine difftest runs
  movies/            input scripts for the tests (converted FCEUX recordings
                     + synthetic random play)
  third_party/
    fceux_headless/  a small driver that builds the FCEUX 2.6.6 core
                     headless as a test oracle (FCEUX source not included)
    SDL3-devel-*-mingw/  SDL3 release for the Windows build (download;
                     git-ignored)
  work/              briefs and outputs of the agent work packages
```

## How it is verified

Full details are in [`re/docs/verification.md`](re/docs/verification.md).

| Layer | Check | Result |
|---|---|---|
| Disassembly | generated listing reassembles byte-identical to the PRG | pass |
| Code/data split | recursive descent from the vectors and 8 dispatch tables. Dynamic coverage (~250k frames) finds no further code | pass |
| Oracle | own emulator vs FCEUX 2.6.6, per-frame RAM, same movies | pass (≤14 one-frame transients per 20k frames, explained) |
| Whole game | C vs original in lockstep: all RAM, SP and the ordered register-access log, compared at every NMI | pass: 10 movies, up to 40k NMIs |
| Per routine | difftest: the original routine and its C version run from recorded and fuzzed states | 200 of 207 pass, 0 fail. 7 are covered by lockstep only (reset, NMI, stack tricks, menu targets that never return) |
| Picture | SDL3 game vs reference emulator, headless replay of the recorded movies | 32/32 pixel-identical |
| Sound | own APU vs FCEUX APU state per frame | pass, apart from ±1-step timing differences |

Original quirks and bugs (8-bit wraparound, table overreads, off-by-ones)
are kept on purpose and marked `QUIRK:` in the source.

## Building and running

Requirements: a C compiler and `make`, Python 3 (standard library only).
For the game you also need SDL3 (`pkg-config sdl3`), or MinGW-w64 for the
Windows build.

### Where to put the ROM

You need one dump of Japanese *Battle City* whose **PRG and CHR** match
these hashes. The 16-byte iNES header is not checked, because it differs
between dumps.

| What | SHA-256 |
|------|---------|
| PRG, 16 KiB (file bytes 16–16399) | `040c74abfdf70f6eecf410651fc0a143ecab3967e321ff5793ca698aa3009d1e` |
| CHR, 8 KiB (file bytes 16400–24591) | `a5d5a10b0fbe9b1a8b8806a06af2634d25ed993b3659131d01acdf86de9c00e9` |
| whole file (the dump this was built from, `BattleCity (Japan) (En).nes`, 24592 bytes, NES 2.0 header) | `166ec235b062aa337045832843e2916f96f12dcf08972ad747016dbe77db761c` |

Check a file with `BC_ROM=/path/to/file.nes python3 re/tools/rom.py`. It
prints `OK` or the hashes it found.

The same file goes in these places. All of them are git-ignored (`*.nes`):

| Used by | Location (first match wins) |
|---------|-----------------------------|
| Python tools, `verify_all.sh`, `check_game.sh`, `check_audio.sh`, `make listing` | 1. `$BC_ROM`, if set<br>2. `battlecity/BattleCity (Japan) (En).nes` (project root, next to this README) |
| `re/Makefile` (the `ROM` variable) | `battlecity/BattleCity (Japan) (En).nes` |
| The game (`battlecity` / `battlecity.exe`) | 1. path given on the command line<br>2. `battlecity.nes` next to the executable (e.g. `re/build/win/battlecity.nes`)<br>3. `battlecity.nes` in the current directory |
| Ghidra / REA (optional) | `python3 re/tools/rom.py --extract` writes `re/rom/prg.bin` and `chr.bin`; open `prg.bin` as raw 6502 at `$C000` |

The simplest setup is to copy the dump to the project root as
`BattleCity (Japan) (En).nes`, and a second copy named `battlecity.nes`
next to the game executable.

### Build and test

```sh
cd re
make listing        # regenerate docs/battlecity.asm, codemap.json and tables from your ROM
make                # build/lockstep, build/difftest, build/nesrun
tools/verify_all.sh # reassembly check + oracle check + lockstep on every movie
python3 tools/difftest.py --lifted --cases 150 --fuzz 6   # per-routine tests
```

The FCEUX oracle step is optional and runs only if
`third_party/fceux_headless/fceux_headless` exists. To build it, unpack the
FCEUX 2.6.6 source somewhere and run
`make -C third_party/fceux_headless FCEUX_SRC=/path/to/fceux-2.6.6`.

### The game

```sh
make game           # Linux: build/battlecity
make game-win       # Windows (MinGW): build/win/battlecity.exe + SDL3.dll
```

Put your ROM next to the executable as `battlecity.nes`, or pass its path:
`build/battlecity [ROM.nes] [--scale N] [--mute]`.

| NES | Keyboard | Gamepad |
|-----|----------|---------|
| D-pad | arrow keys | D-pad |
| A | X | south button |
| B | Z | west button |
| Select | Right Shift | Back |
| Start | Enter | Start |

Esc quits. Two gamepads are supported (player 1 and 2).

## Status / known gaps

- All 207 routines are lifted to C. No mechanically translated code is left.
- 32 data tables are named, each with an exact length taken from the code
  that reads it. Text strings and the data after the level maps are not
  named yet.
- The game front end writes APU registers at fixed points in the frame
  rather than at cycle-exact positions. Game tones correlate ≥ 0.9 with the
  original in 84–91 % of sounding frames.
- The DMC sound channel is not implemented (the game never uses it).

## Licenses / third-party

- **MIT** ([`LICENSE`](LICENSE)) covers the reconstruction sources, tools,
  emulator, harnesses, docs and test data in this repository.
- **GPL-2.0-or-later** ([`re/third_party/fceux_headless/COPYING`](re/third_party/fceux_headless/COPYING))
  covers `re/third_party/fceux_headless/`. That directory builds against the
  FCEUX 2.6.6 source, which is not included. FCEUX is used **only** as an
  external test oracle, and no FCEUX code is copied into `src/` or `tools/`.
- Neither license grants any rights to *Battle City* itself (see the notice
  at the top).
- SDL3 (zlib license) is used by the front end. For `make game-win`, unpack
  the official `SDL3-devel-<version>-mingw` release into `re/third_party/`.
  It is git-ignored; the project was built with 3.4.18.
