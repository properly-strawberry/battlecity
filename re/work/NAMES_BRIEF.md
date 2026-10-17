# Name re-check: brief for each package

Goal: every name in `tools/symbols.py` with status `prior` (imported from an
earlier decomp, never re-checked) gets checked against the code, and either
confirmed (status `verified`) with the evidence, renamed, or left `prior`
with the reason. `unk_XX` names get a real name where the code supports one.

## Sources (read-only)

- `re/src/lifted/*.c`: the whole game as readable C. All 207 routines pass
  per-routine difftest or whole-game lockstep (see `re/docs/verification.md`).
  This is the main source: find every read and write of your variable.
- `re/docs/battlecity.asm`: the listing (comments carry the current names).
- `re/src/ram.h`, `re/tools/symbols.py`, `re/docs/ram_map.md`,
  `re/docs/subsystems/*.md`, `re/docs/tables.md`.
- Movies and tools if you want an observation: `build/nesrun ROM MOVIE
  FRAMES --ram OUT.bin` dumps 2048 bytes of RAM per frame (ROM is
  `../BattleCity (Japan) (En).nes` from `re/`; movies in `re/movies/`).
  Use `build/nesrun`, never `tools/emu/nesrun`.

## Output (the only file you write)

`re/work/<package>/names.tsv`, tab-separated, one line per name in your list:

    addr	current_name	new_name	status	kind	evidence

- `new_name`: the same as current if confirmed; otherwise the better name
  (snake_case, ASCII, no `unk_`/`sub_` unless nothing better is supported).
  For multi-byte variables keep one name for the whole range.
- `status`: `verified` (the code shows what it is) or `prior` (not enough
  evidence; say what is missing in `evidence`).
- `kind`: `observed` (a movie/RAM dump shows it) or `derived` (a conclusion
  from the cited code).
- `evidence`: the code that supports the conclusion, as
  `$ADDR or file:function: what the code does with it` items separated by
  `; `, then `=> conclusion`. One line, at most ~300 characters. Example:
  `$DE0D dec player_lives,x on death; $EA3E inc on extra-life powerup;
  read at $C27A to end the game => lives per player (x = player)`.

Do not edit any other file. Do not commit. When done, reply with the path
of your names.tsv and a short summary: how many verified / renamed / still
prior, and anything unusual you found in the code (with addresses).
