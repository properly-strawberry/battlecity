# Step 3: lifted code calls C functions directly

All 207 routines of Battle City are lifted to C (`re/src/lifted/*.c`) and
verified (per-routine difftest + whole-game lockstep, see
`re/docs/verification.md`). Each routine has an adapter `void sub_XXXX(void)`
that maps the 6502 registers (globals `A X Y fC fZ fN fV`, stack `S`) to its
C function. The adapters are the interface the test harness calls; they stay.

What is left: lifted code still calls other routines through those adapters
(`X = 3; CALL_MECH(sub_D9BE);`), reads results from `A`/`X`/`Y` after a
call, and some jump tables dispatch through `mech_dispatch`/`mech_jump`.
Goal: outside the adapters, your files use plain C: direct calls with real
parameters and return values, locals instead of register globals, and C
`switch`es over the known jump-table targets.

## Your files

Only the files named in your task. Do not edit any other file (others are
being converted at the same time, in other worktrees).

## How

- `re/src/lifted/lifted.h` declares the C function of every routine (home
  file in a comment). `#include "lifted.h"` in your files.
- `re/work/STEP3_ADAPTERS.md`: for each `sub_XXXX`, its C function and its
  register convention (which registers it reads, which it sets). Use it to
  rewrite each `CALL_MECH(sub_XXXX)`: pass what the caller put in A/X/Y as
  parameters; if the caller reads A/X/Y/flags after the call, take the value
  from the C function's return value, or recompute it in the caller from
  RAM if the C function does not return it. You may not change functions in
  other files. If a call cannot be converted without that, leave it, mark it
  `/* step3: kept: <why> */`, and list it in your report.
- Remove local wrappers that only forward to `CALL_MECH` (e.g.
  `static void render_playfield(void) { CALL_MECH(sub_D7B4); }`) and call
  the real function instead.
- Jump tables: a C `switch` on the target address read from the table, one
  case per real target calling its C function. Keep the original's
  behaviour for unreachable targets as the existing code does (comment it).
- Keep every `QUIRK:` comment and the `/* $XXXX */` address comments.
- Adapters (`sub_XXXX`) keep their exact register behaviour. You may make
  an adapter call the same C function as before; do not change what it
  leaves in registers.
- Keep the 6502 stack effects only where a routine's behaviour depends on
  them (e.g. powerup_pickup's abort is already a return value).

## Verify (in your worktree, from `re/`)

The ROM is not in the worktree: `export BC_ROM="/d/Development/reverse/rea-work/battlecity/BattleCity (Japan) (En).nes"`.

1. `make` builds clean (no warnings beyond what was there).
2. `python3 tools/difftest.py --lifted --cases 150 --fuzz 6`: must end with
   `summary: {'pass': 200, 'untestable': 4, 'no-cases': 3}` (0 FAIL).
3. `tools/verify_all.sh 40000`: every movie `lockstep: OK: identical`.
   (The FCEUX oracle line is skipped in a worktree; that is expected.)

Both must pass before you commit. Commit your change in your worktree
branch (one commit; message: what you converted, what you kept and why,
the difftest summary line and the lockstep result), ending with the line
`Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Report

Reply with: your branch name and commit hash; counts before/after of
`CALL_MECH(` and of `mech_dispatch`/`mech_jump` in your files; each kept
call with the evidence why (address / adapter convention); the difftest
summary line and the lockstep lines.
