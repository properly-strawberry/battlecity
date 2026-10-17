# Subroutine index

CPU addresses in the `$C000-$FFFF` PRG window.

Status:
- `pending` — address known (from a `JSR`/`JMP`/vector), body not yet seen
- `done` — annotated pseudo-C exists in `src/`

---

## Wanted next

The eighteen `main_loop` entries are all annotated, and as of session 22 the
reimplementation has enemies in it. What is missing is the frame AROUND a
stage rather than the stage itself — starting it, ending it, and the parts of
the panel that report on it:

1. **`$C71E`** — called by `start_stage` (`$C374`) before the HUD draws. The
   last of `start_stage`'s unread calls now that `$C8C0` is the icon column.
2. **Port the tally screen.** It is now fully read — layout, sprites,
   attributes and count-up — and nothing about it is guesswork any more. It
   needs a second screen mode in the reimplementation, which is the first time
   anything has needed one.
3. **Construction mode** — the last whole subsystem with nothing read.

   It already has hooks everywhere (`$4B`, `edit_tile_sel`, tank slot 0 as the
   cursor), so it is self-contained rather than entangled.

**The sound engine (`$0300` / `$EA7E`) is SKIPPED by decision**, not pending:
there is no plan for APU emulation, so `update_engine_sound` (main_loop 16) and
every `sound_req[]` write stay annotated-but-unported. They are noted where they
occur so nothing looks accidentally missing.

---

## Top-level flow (done)

| Addr | Name | Status | Reached from | Notes |
|---|---|---|---|---|
| `$C070` | `reset` | done | RESET + IRQ vectors | Falls through to the title screen |
| `$C095` | `title_screen` | done | `$C292` | |
| `$C09C` | `title_loop` | done | `$C0AB` | Exit resolved: `title_menu` JMPs out |
| `$C0A2` | `title_loop_body` | done | `$C156`, `$C448` | Construction mode and the attract abort both return here |
| `$C0AE` | `construction_mode` | done | *unknown caller* | Level editor |
| `$C159` | `stage_intro` | done | `$C280` | "STAGE nn" + the level-select cheat |
| `$C1C5` | `play_stage` | done | `$C177`,`$C17D` | Loads the level, runs the frame loop |
| `$C223` | `stage_end_sequence` | done | falls through | Runs until `phase_timer == 2` |
| `$C283` | `game_over` | done | `$C278`,`$C27E` | Returns to the title screen |
| `$C295` | `redraw_screen` | done | `$C28F` | |
| `$C3B5` | `attract_setup` | done | `$C0A5` | Demo setup: stage 30, `game_mode`=2, redraws the logo |
| `$C41D` | `attract_run` | done | `$C0A8` | Demo frame loop; Select/Start aborts via `PLA/PLA` |
| `$C43F` | `attract_abort` | done | `$C424` | Discards its caller's return address, jumps to `$C0A2` |
| `$C44B` | `hiscore_screen` | done | `$C28C` | Draws "HISCORE", waits on the `$0315` jingle |
| `$D16A` | `clear_screen` | done | `$C09C`, `$C445` | off / `$D47E` / `$D7B4` / on. Read from ROM, not pasted |
| `$D17F` | `draw_title_logo` | done | `$C095` | "BATTLE"+"CITY". Read from ROM, not pasted; tail past `$D1B5` still unknown |
| `$C2E6` | `main_loop` | done | `$C200`,`$C23E`,`$C429` | Pure dispatcher: 18 calls, no logic |
| `$C9C0` | `title_menu` | done | `$C0A2` | 1P/2P/CONSTRUCTION. Exits by jump table at `$CA69` |
| `$CA85` | `menu_cursor_y` | done | `$C9CB`,`$CA2F` | `tank_y[0] = menu_sel*16 + $8B` |
| `$D400` | `nmi` | done | NMI vector | OAM DMA, scroll/PPUCTRL, both frame counters |
| `$E181` | `update_tank_terrain` | done | main_loop 1 | Caches each tank's terrain pointer |
| `$DB75` | `player_control` | done | main_loop 2 | Input -> facing; grid snap on turn |
| `$DBF1` | `move_all_tanks` | done | main_loop 3 | Speed gating + the TIMER powerup |
| `$DC3D` | `move_tank` | done | `$DC35` | Dispatch on state high nibble; step + two-corner check |
| `$DD30` | `tank_reverse` | done | `$DD1C` | `dir ^ 2` |
| `$DD48` | `tank_random_turn` | done | state `$9x` | Enemy AI turn |
| `$DD6E` | `clamp_probe_x` | done | `$DCC2`,`$DCE7` | Inclusive → exclusive edge |
| `$DD76` | `clamp_probe_y` | done | `$DCCE`,`$DCF3` | |
| `$D706` | `coords_to_map` | done | `$E19B`,`$DCD2`,`$DCF7` | Pixels → map pointer. Second entry at `$D709` |
| `$D713` | `pixels_to_cells` | done | `$D706` | x,y / 8 |
| `$D5FB` | `cell_to_addr` | done | `$D709` | **Proves the map is at `$0400`** |
| `$E1F3` | `mark_cell_occupied` | done | `$E1C9`,`$E1DA`,`$E1EB` | Sets bit 7 of a cell |
| `$F000` | `load_level` | done | `$C1D9`,`$C3D5` | 13x13 nibbles → the terrain map |
| `$C331` | `start_stage` | done | `$C1F6` | Resets the stage, seeds 20 enemies. Falls into `set_ai_clock` |
| `$E1FA` | `clear_tank_occupancy` | done | main_loop 4 | Mirror of entry 1 — clears bit 7 |
| `$E234` | `unmark_cell` | done | `$E216`,… | |
| `$E02E` | `update_bullets` | done | main_loop 5 | Dispatch on state high nibble |
| `$E063` | `bullet_step` | done | `$E056`,`$E05F` | Two pixels along the heading |
| `$E08C` | `fire_bullet` | done | `$E15A`,`$E178` | Launch; sets speed from tank type |
| `$E0D8` | `draw_bullets` | done | `$C206`,`$C247` | Not main_loop — the play_stage loop |
| `$E122` | `player_fire` | done | main_loop 8 | Handles the two-bullet shuffle |
| `$E162` | `enemy_fire` | done | main_loop 9 | 1 in 32 per enemy per frame |
| `$E604` | `bullet_vs_terrain` | done | main_loop 11 | Two probes, two quarters each |
| `$E693` | `probe_terrain` | done | `$E647`,`$E66D` | Falls through into `hit_terrain` |
| `$E69A` | `hit_terrain` | done | `$E65C`,`$E688` | **The destruction rules** |
| `$E70C` | `bullet_vs_tank` | done | main_loop 13 | Three passes: enemy→player, player→enemy, player→player (stun) |
| `$E910` | `bullet_vs_bullet` | done | main_loop 12 | Player bullets cancel others; `|d| < 6` on both axes, an 11x11 window |
| `$E972` | `powerup_pickup` | done | main_loop 14 | Tail-jumps into `tbl_powerup` |
| `$E8BE` | `spawn_powerup` | done | `$E7D7` | Reuses `powerup_pickup` as a placement test |
| `$E902` | `powerup_coord` | done | `$E8C8`,`$E8D2` | 0..3 → `$30`/`$60`/`$90`/`$C0` |
| `$E9F0` | `powerup_helmet` | done | `tbl_powerup[0]` | `player_shield = $0A` |
| `$E9F5` | `powerup_timer` | done | `tbl_powerup[1]` | `enemy_freeze_timer = $0A` |
| `$E9FB` | `powerup_shovel` | done | `tbl_powerup[2]` | Concrete wall + 20-unit timer |
| `$EA07` | `powerup_star` | done | `tbl_powerup[3]` | `player_level += $20`, cap `$60` |
| `$EA17` | `powerup_grenade` | done | `tbl_powerup[4]` | Kills every live enemy, no score |
| `$EA3E` | `powerup_extra_life` | done | `tbl_powerup[5]` | `INC $51,X` |
| `$E23B` | `draw_powerup` | done | `$C203`,`$C241` | Blink, and the collect flash |
| `$E2A9` | `shovel_and_base` | partial | main_loop 6 | Shovel tick + eagle explosion |
| `$DDEA` | `tank_dying` | done | states `$1x`-`$7x` | Explosion, then respawn or free the slot |
| `$DE55` | `tank_spawn_phase1` | done | state `$Fx` | 14 frames, then `$E0` |
| `$DE64` | `tank_spawn_phase2` | done | state `$Ex` | 14 frames, then `init_tank_type` |
| `$DE46` | `arm_game_over_banner` | done | `$DE30`,`$DE42` | Per-player variant |
| `$C728` | `stage_over` | done | `$C21E` | Three exits; arms the centred banner |
| `$C2A2` | `clear_pads_if_base_lost` | done | `$C23B` | Zeroes both pads every frame while the eagle is dead |
| `$C2B3` | `new_game` | done | `$CA78` | Scores, levels, 3 lives each, stage 1 |
| `$D9FE` | `clear_score_digits` | done | `$C2B5`,`$C2BA` | 7 zeros + the `$FF` terminator |
| `$CCD4` | `stage_tally` | done | `$C256` | The count-up; one kill per frame per player |
| `$D276` | `delay_frames` | done | `$CCD9`,`$CDD8`,… | Wait X frames, running `$D0B8` each one |
| `$CEF7` | `build_tally_screen` | done | `$CCD4` | The static layout, on the SECOND nametable |
| `$D0B8` | `draw_tally_sprites` | done | `$CCFD`,`$D27B` | The four kind tanks, every frame |
| `$D0D9` | `set_tally_attributes` | done | `$CF18` | Attribute bytes into `$07C0`-`$07D5` |
| `$D130` | `draw_tally_tank` | done | `$D0C0`,… | One 16x16 tank, x fixed at `$81` |
| `$EA51` | `sound_reset` | done | `$C15C`,`$C253` | APU re-arm + clears page 3; the only sound routine read |
| `$C8F9` | `draw_pause_banner` | done | `$C21B` | The blinking PAUSE, five 8x16 sprites |
| `$C947` | `draw_game_over_banner` | done | `$C9AC` | GAME over OVER, two 16x16 objects -- CONFIRMED from the CHR |
| `$D53E` | `load_sprite_palettes` | done | ? | PPUADDR = `$3F10`, copies `tbl_sprite_palette` |
| `$D50E` | `load_bg_palette` | done | `$D41A` (NMI) | Row `palette_request` -> `$3F00`, then disarms itself |
| `$C894` | `hud_enemy_cell` | done | `$C8A2`,`$C8B1` | Icon index -> panel cell: col 29+(n&1), row 3+(n>>1) |
| `$C8A2` | `draw_enemy_icon` | done | `$C8C6` | One row of two icons, tile run `$D362` |
| `$C8B1` | `update_enemy_hud` | done | `$DB68` | ERASES one icon; index is the post-decrement count |
| `$C8C0` | `draw_enemy_column` | done | `$C377` | All twenty at stage start, n = 18 down to 0 by twos |
| `$C972` | `game_over_slide` | done | main_loop 15 | Slide, hold, park |
| `$E27C` | `draw_shield` | done | main_loop 7 | Also ticks `player_shield` down |
| `$C31D` | `palette_cycle` | done | main_loop 18, `$C24A` | Flips `palette_request` 2↔1 |
| `$C7C8` | `draw_hud` | done | main_loop 17 | Labels + lives, shown minus one |
| `$D9FE` | `clear_digits` | done | `$D9E3`,`$DA17`,`$CD02`,`$CD07` | Zero 7 digits + `$FF` |
| `$D9E1` | `set_score_popup` | done | `$E81B`,`$E9B6`,`$CD1F` | Packed BCD hundreds → digits |
| `$DA13` | `binary_to_digits` | done | `$C80C` | Small binary → tens and units |
| `$D9BE` | `add_digits` | done | `$E824`,`$E9BD`,`$CD36` | Add the popup into buffer X |
| `$D97D` | `update_hiscore` | done | `$C286` | Returns who set the record in Y |
| `$D138` | `check_extra_life` | done | `$E827`,`$E9C0`,`$CD3D` | A life at 20,000 |
| `$CCD4` | `stage_tally` | partial | `$C256` | The between-stages count-up |
| `$DA2B` | `draw_sprite_oam` | done | `$DA64`,`$DA7B` | Writes one OAM entry; applies the trees bit |
| `$D725` | `quarter_mask` | done | `$E69A` | **Proves the brick quarter mask** |
| `$D73C` | `test_quarter` | done | `$E69D` | |
| `$D743` | `clear_quarter` | done | `$E6FA` | |
| `$C391` | `set_ai_clock` | done | falls through | **Starts here, not `$C39C`** — the head is a lap check |
| `$DB0B` | `update_engine_sound` | done | main_loop 16 | Maintains `sound_req[$11]` |
| `$DB38` | `player_is_driving` | done | `$DB12`,… | Alive and holding a direction |
| `$DB48` | `spawn_enemy` | done | main_loop 10 | Cooldown + free-slot search |
| `$E363` | `spawn_tank` | done | `$C347`,`$C350`,`$DB61` | Place a tank; flags bonus enemies |
| `$E3B8` | `init_tank_type` | done | ? | Facing + `tank_type` once the tank goes live |
| `$D80B` | `draw_block` | done | `$F058` | One 16x16 block → a 2x2 cell group |
| `$D784` | `write_cell` | done | `$D836`,… | Stores a cell AND queues the PPU write |
| `$D7AA` | `ptr_advance` | done | `$F020`,`$D83B`,… | `$11`/`$12` += A |
| `$DE72` | `pick_direction` | done | `$DC93`,`$DD6A` | Picks an AI mode from the clock |
| `$DE8E` | `pick_player_target` | done | falls through | Even slots hunt p1, odd hunt p2 |
| `$DD7E` | `seek_target` | done | states `$Bx`/`$Cx`/`$Dx` | Three entry points, shared tail |
| `$DDA2` | `heading_to_target` | done | `$DD9C` | 3x3 sign lookup |
| `$DAAF` | `sign_of` | done | `$DDA7`,`$DDB4` | -1 / 0 / +1 |
| `$E420` | `set_tank_mode` | done | many | Replaces `tank_state`'s high nibble |
| `$E42B` | `load_enemy_counts` | done | ? | Per-stage enemy mix from `$E578` |

## Pending

| Addr | Name | Called from | Best guess |
|---|---|---|---|
| `$C2A2` | `clear_pads_if_base_lost` | `$C23B` | Drives `phase_timer` |
| `$C2BD` | `sub_C2BD` | `$C3BD` | First thing attract setup calls |
| `$C331` | `sub_C331` | `$C1F6` | Pre-gameplay setup |
| `$C5D9` | `sub_C5D9` | `$C283` | Game-over screen. Draws strings at `$D343` and `$D348` |
| `$C642` | `sub_C642` | `$C426` | Attract demo's input source (guess) |
| `$C6C6` | `sub_C6C6` | `$C144` | Construction: place tile / move cursor |
| `$C6D2` | `sub_C6D2` | `$C0ED` | Construction per-frame |
| `$C7AB` | `sub_C7AB` | `$C09F` | Title setup |
| `$C8D0` | `sub_C8D0` | `$C0F0` | Construction per-frame |
| `$C8F9` | `sub_C8F9` | `$C21B` | Gameplay per-frame |
| `$C9B0` | `sub_C9B0` | `$C0B5`, `$C1D4` | Shared by construction + level load |
| `$CA91` | `sub_CA91` | `$C172` | Stage intro per-frame; probably ticks `$4C` |
| `$CAF5` | `sub_CAF5` | `$C0E7`, `$C1DC` | Shared by construction + level load |
| `$CB5D` | `load_custom_level` | `$C1E2` | Loads the construction-mode level |
| `$CC27` | `sub_CC27` | `$C1E9` | |
| `$CC90` | `sub_CC90` | `$C16F` | Draws the stage-intro screen |
| `$CCB2` | `sub_CCB2` | `$C1EC` | |
| `$D467` | `rendering_on` | `$C092`,`$C0BB`,`$C29E`,… | GUESS |
| `$D470` | `rendering_off` | `$C0B2`,`$C295`,`$C3E0`,… | GUESS |
| `$D47E` | `clear_playfield` | `$C298`,`$C458`,`$D175`,`$D186` | GUESS — clear/prepare the screen buffer |
| `$D491` | `sub_D491` | `$C089` | Hardware + RAM init |
| `$D7B4` | `render_playfield` | `$C0B8`,`$C29B`,`$C409`,… | CONFIRMED s18 — pushes all $0400-$07FF at the PPU, one cell at a time |
| `$D7CC` | `reset_terrain_map` | `$C9BC` | **Fills the map with `$11`, then clears the playfield. This IS the bounds system** |
| `$C9B0` | `clear_playfield` | ? | Sets (2,2) and 26x26, then `reset_terrain_map` |
| `$D771` | `write_cell_direct` | `$D7BD` | Un-queued PPU write; high byte is `ptr_hi + screen_mode` |
| `$D8D2` | `draw_text_bricks` | `$C3F3`,`$C406`,`$C46B`,`$D199`,`$D1AC` | x=`$56` y=`$57` ptr=`$13`/`$14` |
| `$D612` | `sub_D612` | `$D81E` | Writes the PPU attribute entry for a block |
| `$D8F6` | `wait_frame` | everywhere | Vblank sync — top of every loop |
| `$DEA6` | `draw_all_tanks` | `$C0F9`,`$C209`,`$C244` | 8 slots via `tbl_tank_draw` ($E4B8); blink-gated in construction |
| `$DEB8` | `draw_tank` | `$DEAC` | One slot, `JMP (tbl_tank_draw + (state >> 4) * 2)` |
| `$DFB6` | `draw_tank_body` | `tbl_tank_draw` `$8x`-`$Dx` | A live tank. Picks the palette — and skips a STUNNED player on half the frames |
| `$E00B` | `draw_spawn_twinkle` | `tbl_tank_draw` `$Ex`/`$Fx` | Four frames `$A1 $A5 $A9 $AD`, pulsed by `\|n-7\|` |
| `$DECD` | `draw_explosion` | `tbl_tank_draw` `$5x`-`$7x` | 16x16, tiles `$F1 $F5 $F9` |
| `$DEF0` | `draw_explosion_frame` | `$DEE2`,`$DF2B`,`$DF3E` | Second entry to `$DEE2`, frame passed in |
| `$DEFD` | `draw_death_tail` | `tbl_tank_draw` `$1x` | Last puff, or the floating score for an enemy |
| `$DF33` | `draw_explosion_last` | `tbl_tank_draw` `$2x` | Tile `$F9` once more |
| `$DF46` | `draw_explosion_big` | `tbl_tank_draw` `$3x`,`$4x` | 32x32 as four 16x16s, 8 hardware sprites |
| `$DF99` | `big_explosion_tile` | `$DF4E`,`$DF60`,`$DF72`,`$DF84` | Quadrant tile: `$D1`/`$E1` base + q*4 |
| `$C830` | `draw_hud_labels` | `$C37D` | "IP" at (29,17), "IIP" at (29,20); 2P only for the second |
| `$C859` | `draw_hud_flag` | `$C380` | Flag at (29,23)-(30,24), stage number on row 25 |
| `$D6B3` | `draw_tiles` | `$C838`,`$C851`,`$C868`,… | A run of tiles at a cell: into the map AND the queue |
| `$D6DD` | `draw_hud_number` | `$C88C`,`$C816` | Queue only, `+ digit_tile_base` per byte |
| `$DA13` | `number_to_digits` | `$C880`,`$C80C` | n -> the `SCORE_POPUP` digit buffer |
| `$D934` | `align_number` | `$C887`,`$C813` | Skips leading zeros, walking the screen column with them |
| `$E409` | `sub_E409` | `$C331` | Clears `$CC-$D5` |
| `$CAF5` | `build_eagle_brick` | `$C0E7`,`$C1DC`,`$C412`,`$E2CF` | The eagle's default brick surround — why it is absent from level data |
| `$CB9E` | `build_eagle_concrete` | `$E9FF`,`$E2C9` | The Shovel's version |
| `$DA73` | `draw_sprite16` | `$DFFF` | Folds a facing index into the tile, then falls into `$DA7B` |
| `$DA7B` | `draw_sprite16_at` | `$E274`,`$E02A`,`$DEF9`,`$DF1D`,`$DF51`,`$E2A1` | Every 16x16 object in the game |
| `$CC08` | `sub_CC08` | `$E6BA` | Draws the destroyed eagle |
| `$DA64` | `draw_sprite` | `$E10E` | |
| `$DEE2` | `draw_explosion_sprite` | `$DEDA` | Frame from the state's high nibble |
| `$D74D` | `sub_D74D` | ? | Clear a quarter, but only if the high nibble is 0 |
| `$D764` | `sub_D764` | ? | Set a quarter, same guard |
| `$E413` | `clear_tank_slots` | `$C0BE`,`$C334`,`$C9C4` | CORRECTED s18 — draws nothing; zeroes `tank_state[]` and `player_slide[]` |
| `$EA51` | `sound_reset` | `$C15C`,`$C253` | |
| `$C2B3` | `new_game` | `$CA78` | Only caller is the menu commit — probably "start a new game" |
| `$C49C` | `sub_C49C` | `$CA4F` | Payload of the hidden two-controller cheat |
| `$DB0B` | `update_engine_sound` | main_loop 16 | |
| `$DB48` | `spawn_enemy` | main_loop 10 | |
| `$D50E` | `sub_D50E` | `$D41A` | NMI-side, skipped when `palette_request` bit 7 set |
| `$D689` | `sub_D689` | `$D433` | NMI-side |
| `$D8FD` | `sub_D8FD` | `$D413` | GUESS — VRAM update queue flush; runs in vblank right after the latch reset |
| `$DA93` | `sub_DA93` | `$D436` | NMI-side |
| `$D44D` | `rng` | `$DC8C`,`$DD17`,`$DD48`,`$DDD4` | GUESS — random number generator |
| `$E451` | `decode_direction` | `$DB9F` | Buttons → facing 0..3, negative if none |
| `$EA7E` | `sub_EA7E` | `$D439` | GUESS — the sound driver. NMI-side, so it is what clears `sound_req` |

## Data tables

| Addr | Name | Size | Description | Evidence |
|---|---|---|---|---|
| `$CA69` | `tbl_menu_targets` | 6 | Three little-endian jump targets indexed by `menu_sel*2`: `$CA6F` 1P, `$CA74` 2P, `$CA7E` construction | read from ROM |
| `$D299` | string table | — | `$FF`-terminated ASCII | read from ROM |
| `$E46C` | `tbl_dx` | 4 | `00 FF 00 01` — dx per direction | read from ROM |
| `$E470` | `tbl_dy` | 4 | `FF 00 01 00` — dy per direction | read from ROM |
| `$E474` | spawn points | 10 | enemy X `18 78 D8` / Y `18 18 18`, player X `58 98` / Y `D8 D8` | GUESS |
| `$E486` | `tbl_heading` | 18 | Two 3x3 grids of `$A0`-`$A3`: prefer-vertical then prefer-horizontal steering | read from ROM |
| `$E498` | `tbl_state_handlers` | 32 | `move_tank` dispatch, `(state >> 4) * 2` | read from ROM |
| `$E4B8` | `tbl_tank_draw` | 32 | `draw_tank` dispatch, same index — runs in parallel with the above | `$DEC0` |
| `$E47E` | `tbl_slot_facing` | 8 | Starting `tank_state` per slot: players up, enemies down | `$E3B8` |
| `$E003` | `tbl_enemy_palette` | 8 | `02 00 00 01 02 01 02 02` — sprite palette for a non-bonus enemy, indexed `(frame * 4 + type) & 7`, so each type flickers between two entries | `$DFD7` |
| `$E8BA` | `tbl_enemy_score` | 4 | `10 20 30 40` — BCD hundreds per enemy kind | `$E81B` |
| `$E8FA` | `tbl_powerup_kind` | 8 | Weighted type draw: `00 01 02 03 04 05 04 03`. Star and Grenade twice as likely | `$E8EC` |
| `$E9E2` | `tbl_powerup` | 14 | The six effect handlers plus one unused | `$E9CE` |
| `$E4D8` | `tbl_bullet_move` | 10 | `update_bullets` dispatch, 5 entries | `$E044` |
| `$E4E2` | `tbl_bullet_draw` | 10 | `draw_bullets` dispatch, 5 entries. Back to back with the above, filling `$E4D8-$E4EB` | `$E0EE` |
| `$D3D1` | `tbl_tally_score` | 4 | `10 20 30 40` again, for the end-of-stage count-up | `$CD1C` |
| `$D3D5` | `tbl_banner_dx` / `dy` | 8 | A third copy of the direction deltas, for the GAME OVER banner | `$C998` |
| `$EA49` | `tbl_dx2` / `tbl_dy2` | 8 | A second copy of the direction deltas, used by `bullet_vs_terrain` | `$E622` |
| `$E4EC` | `tbl_enemy_type` | 140 | 35 stages x 4: which kind each group is | `$E3F1` |
| `$E578` | `tbl_enemy_counts` | 140 | 35 stages x 4: how many of each. Sums to 20 every stage | `$E43C` |
| `$DABB` | `tbl_block_attr` | 16 | Palette index per block type | `$D817` |
| `$DACB` | `tbl_block_cells` | 64 | The 2x2 cell pattern per block type. With the above, exactly fills `$DABB-$DB0A` | `$D832` |
| `$F07A` | level data | 3276 | 36 entries x 91 bytes. 13x13 blocks, one nibble each. `rom.py level N` decodes one | `$F012` |

### Data regions Mesen marked in `asm_snapshots/01.asm`

Its tracer's opinion, so "unidentified" means "never reached", not "definitely
data". Still a good map of where the tables are.

| Range | Marker | Likely |
|---|---|---|
| `$C49C`–`$C5D8` | unidentified | The title cheat payload — never traced, consistent with it being unreachable in normal play |
| `$C6C2`–`$C6C5` | data | |
| `$C755`–`$C7AA` | unidentified | |
| `$CA69`–`$CA6E` | data | `tbl_menu_targets` — confirmed |
| `$D284`–`$D3FF` | unidentified | Contains the `$D299` string table — confirmed |
| `$D555`–`$D5F4` | data | |
| `$DABB`–`$DB0A` | data | |
| `$E306`–`$E311` | data | |
| `$E46C`–`$E603` | data | 408 bytes, sits right after `decode_direction` |
| `$E8BA`–`$E8BD`, `$E8FA`–`$E901`, `$E9E2`–`$E9EF`, `$EA49`–`$EA50` | data | small tables |
| `$EBE6`–`$EC09` | data | |
| `$ECE6`–`$EFFF` | data | 794 bytes |
| `$F07A`–`$FFFF` | data | **3974 bytes right after `load_level` `$F000` — the stage maps** |
