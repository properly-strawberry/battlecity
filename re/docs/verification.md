# Verification status

Everything here is reproducible with `re/tools/verify_all.sh [FRAMES]`.

## Layers

| # | Claim | How it is checked | Status |
|---|---|---|---|
| 1 | The disassembly listing (`docs/battlecity.asm`) is the ROM | `tools/asm_check.py`: two-pass reassembly → byte-identical to `rom/prg.bin` (16384 bytes). A one-byte mutation is caught (negative test done) | **pass** |
| 2 | Code/data separation is complete | Static recursive descent from vectors + 7 jump tables + 1 inline dispatch table. Dynamic coverage from 6 gameplay/idle movies (~250k frames) adds **no** code beyond static | **pass** (bounded by movie coverage) |
| 3 | The reference emulator (`tools/emu`) is correct for this game | Per-frame RAM vs FCEUX 2.6.6 core (`third_party/fceux_headless`), same movies, same power-on RAM | **pass**: no persistent difference; ≤14 transient frames per 20k frames (see below) |
| 4 | The C reconstruction behaves like the ROM | `build/lockstep`: at every NMI compares all RAM (except the dead stack below SP), SP, and the ordered hardware-register access log since the previous NMI | **pass**: 6 movies × 20k NMIs, both power-on RAM patterns |
| 5 | Each lifted (hand-written) routine matches the original | Per-routine differential fuzzing vs the emulator, frame-aware (`tools/difftest.py --lifted --cases 150 --fuzz 6`), plus layer 4 | **pass**: all 207 routines lifted; 200 pass difftest (0 fail), 7 covered by lockstep only (see below) |
| 6 | The SDL3 game shows what the original shows | `tools/check_game.sh`: each user movie replayed through `build/battlecity --headless` (NMI k gets the pads of the original's k-th NMI frame, from `nesrun --nmipc`); the picture after NMI k vs the reference emulator's picture of that frame | **pass**: 8 pictures × 4 movies, pixel-identical. Windows build (`make game-win`, under wine) gives the same picture as native |
| 7 | The game's sound is the original's sound | `tools/check_audio.sh`. (a) APU model: `build/apucheck --fceux-timing` feeds our APU the original's register writes at their CPU cycle; its channel state at every frame end (pulse periods, length counters, envelopes, triangle linear counter) vs FCEUX 2.6.6's saved state (`fceux_headless --apustate`), `tools/apu_state_compare.py`. (b) Game: `build/battlecity --headless --audio` vs the same APU at the original's cycles, tones only, per-frame correlation | **(a) pass** on all 4 user movies: 95-100% of frames identical per field, every difference exactly one step (see "Sound" below). **(b) report**: 84-91% of sounding frames correlate >= 0.9, mean 0.95-0.97, level ratio 1.004-1.006 |

## Lifted routines (layer 5), 2026-10-15 (all 207)

`src/lifted/*.lifted`, results in `evidence/difftest_lifted.json`. All
packages also pass layer 4 (10 movies, up to 40k NMIs, official build).

| Package | Routines | difftest |
|---|---|---|
| bullets | 34 | 33 pass, 1 untestable ($E972 powerup_pickup: pla/pla) |
| cells | 19 | 19 pass |
| flow | 19 | 14 pass, 3 no-cases, 2 untestable (RESET $C070, NMI $D400) |
| flow_hud | 21 | 21 pass |
| gfx_text | 21 | 21 pass |
| screens | 15 | 15 pass |
| sound | 18 | 17 pass (4 from surrogate states, see below), 1 untestable ($ECD0) |
| sprites | 26 | 26 pass |
| tanks | 34 | 34 pass |

- **Frames in difftest.** Time passes only where the program waits for
  vblank, the same model lockstep uses (see "Timing model" below). The
  original waits in `wait_frame` ($D8F8) and `wait_vblank_flag` ($D5F5/$D5F8);
  there the harness runs pre-render and vblank start, and takes the NMI if
  enabled. The C side does the same in `hal_wait_vblank` and calls
  `bc_nmi()`. Both must consume the same number of frames. This made 30
  routines that only lockstep covered testable per routine.
- **no-cases** (3): `$CA6F`/`$CA74`/`$CA7E`, the title menu's mode targets.
  Each runs a whole game mode and does not return within 4000 frames. They
  are verified by whole-game lockstep only, along paths the movies reach.
- **Input domains.** Fuzzed states outside the values a routine is ever
  called with count as invalid, not as failures (`in_domain()` in
  `tools/harness/difftest.c`, each with its reason). One so far:
  `load_level` ($F000) takes stage 1..70 or $FF; larger values advance the
  map pointer past $FFFF and the original reads its own stack page as map
  data.
- `$E972` powerup_pickup: on a pickup the original does pla/pla and
  tail-jumps to the effect, so `main_loop` skips its last four calls for
  that frame. The C returns 1 in that case and `main_loop` returns early.
  Untestable per routine (it discards the harness's return address);
  lockstep covers it (every movie with a pickup, first at NMI 1772 of
  `fceux_playthrough1`).
- **Global fuzz invariant:** fuzzed states keep `bullet_state` < $50, as
  every writer in the game does (`in_domain()` in difftest.c lists them).
- **Surrogate entry states:** sound commands $E9/$EB/$EC/$ED ($EC21, $EC45,
  $EC57, $EC61) are never reached: a walk of all 28 scripts (following jumps
  and both loop exits) finds only $E8 $EA $EE $EF $F0 $F1 $F2 $F9. They are
  tested from the entry states of sibling handlers ($EC33, $EC6B), which the
  same dispatcher enters in the same context (`difftest.py` SURROGATE).
- `$ECD0` (inline dispatch after the JSR at $EBE3) reads its caller's
  return address; untestable per routine. Its only caller runs in C
  (`run_script()` in sound.c), so the adapter serves register-level code only.
- The credits easter egg ($C49C, $C567, $C575, $C5B0; see
  `docs/subsystems/screens.md`) is lifted and passes difftest and lockstep.
  Only `fceux_easteregg` (user recording) reaches it (coverage shows all
  four routines executed).
- `load_custom_level` ($CB5D) is reached only by `fceux_2p_ice`
  (construction mode, then play): 1 captured entry, 7 cases, all pass.

## Sound (layer 7), 2026-10-15

- **FCEUX's frame counter is not the hardware's.** FCEUX steps every
  7457.5 CPU cycles with an empty fifth step, so its 5-step period is
  37287.5 cycles; the hardware's is 37282 (observed: phase drifts ~5.5
  cycles per period between them; after 800 frames FCEUX's steps lag by
  ~4300 cycles). Our APU uses the hardware sequence; `--fceux-timing`
  switches to FCEUX's for the comparison only.
- **Residual one-step differences vs FCEUX** (with --fceux-timing): the step
  positions still differ by up to ~100 cycles, so a clock can fall on the
  other side of a register write or of the frame end. Observed on
  fceux_playthrough1: every count difference is +-1; length counters for 1
  frame, envelopes for 2-6 frames (until the next note write reloads them).
- **Polarity and filters:** FCEUX's output is inverted relative to ours
  (`audiocmp --invert`). Our filters are the Famicom's (37 Hz high-pass,
  14 kHz low-pass); the NES's extra 440 Hz high-pass removed the engine
  rumble that FCEUX (and the Famicom) keep.
- **FCEUX low-quality mode mixes triangle and noise together,** so muting
  one channel there does not isolate the other; the dumps use high quality.
- **Waveform correlation is not a pass/fail test:** noise is random; pulse
  and triangle phases depend on history; two pulses with different phase
  offsets cannot be aligned by one lag. The per-frame state comparison is
  the strict test; correlation is reported.
- **Sub-frame timing in the game:** the C code has no cycle timing inside a
  frame. The NMI's APU writes land at cycle 2513 of the frame (measured
  median, range 1433-5352) and the main thread's at 3984 (the most common
  $4017 position; others at ~9500-12800). See `src/platform/main_sdl.c`.

## Known, explained differences

- **Oracle transients:** FCEUX and the reference emulator differ in at most
  14 of 20000 frames per movie, never for more than 1 consecutive frame,
  and only in `$0011` / `$005D`. These are frames in which the main thread
  runs with NMI disabled (screen building) and is caught mid-loop at the
  frame boundary. Sub-instruction timing differs between the emulators.
  The game state reconverges in the next frame.
- **Stack bytes `$0160-$017F`:** the NMI pushes the interrupted PC, which is
  `$D8F8` or `$D8FA` (both inside the wait loop), depending on cycle timing.
  These bytes are never read as data.

## Timing model of the reconstruction

Measured over 120k frames of play: every NMI interrupts the main thread
inside the wait-for-frame loop at `$D8F8`/`$D8FA`, or NMI is disabled. The game
never lags. So the C model is exact at the NMI level: the main thread
runs until it waits (`hal_wait_vblank`), then the NMI handler runs.
Not modelled: how many frames a rendering-off screen rebuild takes in the
original. The reconstruction finishes it within one wait.

## Power-on RAM

The game reads RAM it never initialises (295 bytes in a 20k-frame run, mostly
through the RNG at `$D44D`, which stirs in a byte selected by `rng_stir`; also
`$0A`, `$0B`, `$0F`, `$10`). The whole run depends on power-on RAM. All tools
default to FCEUX's pattern (`00 00 00 00 FF FF FF FF` repeating) so that FCEUX
recordings replay; `--raminit 2` selects all zeros.

## Movies

`re/movies/*.txt`, frame-indexed (`COUNT P1 [P2]`, letters `ABsSUDLR`).
`synthetic_*` are random-play scripts from `tools/mkmovie.py`. FCEUX
recordings (`.fm2`, power-on) convert with `tools/fm2_to_movie.py`.
