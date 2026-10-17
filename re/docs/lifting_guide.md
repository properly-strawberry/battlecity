# Lifting guide: turning 6502 routines into readable, verified C

Audience: an agent working one **work package** (a list of routine
addresses). Read this whole file first. Reference implementation:
`src/lifted/cells.c` (19 routines, all verified). Copy its style.

## What already exists (do not change these)

| Path | What |
|---|---|
| `../BattleCity (Japan) (En).nes` | original ROM; never modify |
| `docs/battlecity.asm` | verified disassembly with names (reassembles byte-identical) |
| `docs/contracts.json` | per routine: `in` = registers/flags read, `out` = registers/flags callers use afterwards, `preserves` = live registers it must not change |
| `tools/symbols.py` | names for RAM and code. `prior` = imported from the user's old decomp, **unverified** |
| `src/ram.h` | generated `#define`s: every RAM variable at its original address |
| `src/machine.h` | `RAM[]`, `PRG[]`, `bus_read/bus_write`, HAL (`hal_read/hal_write/hal_wait_vblank`) |
| `src/lifted/common.h` | helpers: `ZPTR`, `set_zptr`, `ind_y`, `ind_y_store` |
| `src/mech/mech.c` | GENERATED instruction-exact translation of every routine not yet lifted |
| `../docs/*.md` | user's notes from an earlier decomp: good hints, **verify before trusting** |

## Your outputs (only these files)

All in `work/<package>/` (create it):

1. `<package>.c`: the lifted C (`#include "../../src/lifted/common.h"`).
2. `<package>.lifted`: one hex address per line, each routine your `.c`
   replaces. List only routines that pass difftest.
3. `notes.md`:
   - **Names:** routine and RAM names you propose or confirm, each with
     evidence (`$addr: what the code does`). Mark observed vs derived.
   - **QUIRKs:** bugs and oddities found.
   - **Not lifted:** routines left out, and why.
   - **Unknowns:** open questions.

Do **not** edit anything outside `work/<package>/`. The coordinator merges
names into `symbols.py` and moves your files into `src/lifted/`.

## How to write a lifted routine

For each routine `$XXXX`:

```c
/* $XXXX name_of_routine
 * One or two lines: what it does in game terms.
 * Inputs/outputs in plain words. Side effects worth knowing. */
RET name_of_routine(params...) { ...readable C... }
void sub_XXXX(void) { /* register adapter */ A = name_of_routine(X, Y); ... }
```

Rules:

- **Exact RAM effects.** Every byte the original writes must be written
  with the same value, including scratch bytes ($00-$04, cell_x, ...) and
  registers saved in RAM. Writes that land on the same final state need not
  happen in the same order, except **hardware register accesses**
  (`hal_read`/`hal_write`), whose order and values must match exactly.
- **8-bit arithmetic.** Use `uint8_t` and explicit casts; reproduce
  wraparound, carries, BCD-style tricks and index wrapping (`abs,X` with
  base+X > $FFFF; `zp,X` wraps within zero page; `RAM[0x180 + x]` with
  uint8 x). Use `bus_read/bus_write` for indexed accesses that may leave
  $0000-$07FF (mirrors).
- **Adapter.** `void sub_XXXX(void)` takes inputs from the globals
  `A X Y fC fZ fN fV` as listed in the contract's `in`. It must set every
  register/flag in the contract's `out` exactly as the original leaves it
  (`SZN(v)` sets Z and N). It must not change registers in `preserves`.
  The adapter is how mechanical code and the test harness call you.
- **Calling other routines.** Inside your package, call your own C
  functions directly. For a routine owned by another package (or not yet
  lifted), set its input registers (`A = ...; X = ...;`), call it with
  `CALL_MECH(sub_XXXX);` and read its outputs from the globals. This works
  whether that routine is mechanical or lifted by someone else, and keeps
  the 6502 stack balanced. Never call `sub_XXXX()` of another package
  without `CALL_MECH`. Your own routines' adapters exist for the other
  direction.
- **Jump tables** (`JMP (ptr)` after loading from a table) become a `switch`
  or a table of C function pointers. Keep table **data** read from ROM
  (`ROM8(addr)`) if the index can come from game state.
- **Readable names.** Name locals and functions after their game meaning.
  Comment the non-obvious. Mark original bugs and oddities with
  `QUIRK:` and keep them.
- **Routines that wait for vblank** (call `$D8F6` / `hal_wait_vblank`), and
  routines with `pla pla` stack aborts, are the coordinator's. Skip them
  unless your package explicitly includes them.

## How to verify (mandatory)

```sh
cd re
make agent MOD=<package>                         # builds build/<package>/
python3 tools/difftest.py --mod <package> --cases 150 --fuzz 6
MOD=<package> tools/verify_all.sh 20000          # whole-game lockstep, all movies
```

- `difftest` runs each listed routine from captured real states plus fuzzed
  variants on the original and on your C, and compares RAM, output registers
  and the hardware access log. Expect `pass` for every routine.
  `skipped`/`invalid fuzz` counts are fine. A `FAIL` is a bug in your C.
- `verify_all.sh` must print `OK: identical` for every movie.
- `unreached` means no movie executes the routine, so it cannot be tested.
  Keep such routines **out** of `.lifted` and list them in notes.md as
  untested.

Iterate until both pass. Only then report back. Your report must
include: routines lifted, any you skipped, names proposed, the difftest summary
line, and the lockstep result. For each name or behaviour claim, cite the
evidence (instructions/addresses, table bytes, callers, test results) and the
conclusion it supports, marked observed vs derived. For every skipped routine
and every non-obvious restructuring (jump tables, stack tricks, QUIRKs), give
the evidence-backed justification. notes.md must contain these evidence →
conclusion entries in full; the report may summarise them. The coordinator
checks your work from this; claims without cited evidence cannot be checked.

## Useful tools

- Read the listing: `sed -n '/^name:/,/rts/p' docs/battlecity.asm` or
  `python3 tools/dis_quick.py C000 C100` (raw linear disassembly).
- `src/mech/mech.c` shows the exact semantics of each instruction if unsure.
- Contracts: `python3 -c "import json;print(json.load(open('docs/contracts.json'))['D44D'])"`
- REA MCP tools (Ghidra pseudocode, xrefs) can help but are optional; the
  listing is authoritative.
