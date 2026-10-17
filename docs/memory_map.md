# Battle City — memory map

**Generated from `src/globals.h` by `python tools/codemod.py memmap`.**
Do not hand-edit: edit the declaration in `globals.h` and regenerate.
`globals.h` carries the full reasoning; this is the address index.

Confidence is marked inline: `(guess)` / `(?)` / no marker = read straight
off the asm.

## Zero page

| Addr | Symbol | Description |
|---|---|---|
| `$00` | `zero_page` | -$FF seen as raw bytes, which rng() does when it stirs an arbitrary zero-page byte into its state |
| `$00` | `scratch_00` | scratch. player_control keeps the decoded direction here; load_level keeps the stage index. |
| `$04` | `scratch_04` | scratch: the block's palette index in draw_block |
| `$05` | `screen_mode` | (?) selects which screen gets built. $1C blank/menu/hiscore/stage-intro, $24 title logo. |
| `$06` | `joy_held` | buttons held, INDEXED BY PLAYER ($06 p1, $07 p2). Confirmed by `LDA joy_held,X` at $DB9D with X = the player slot. |
| `$08` | `joy_pressed` | buttons newly pressed this frame ($08 p1, $09 p2). $09 being a separate source is confirmed by the title cheat, which needs $06 and $09 at once. |
| `$0A` | `phase_timer` | incremented by the NMI every 64 frames ($D444), so it ticks at ~1.07 s. Used as a coarse wall clock: end-of-stage sequence waits for == 2   (~2 s) title menu idle timeout waits for $0A  (~11 s) Reset to 0 on menu input ($CA00). |
| `$0B` | `frame_counter` | incremented by the NMI every frame ($D43C). Also zeroed by input handlers to restart auto-repeat. Drives blink (& $10) and auto-repeat (& $07). |
| `$0C` | `vram_queue_len` | write index into vram_queue. Cleared before the gameplay loop, and after every block during a level load (the screen is fully redrawn afterwards anyway). |
| `$0D` | `oam_index` | write index into oam_buffer ($DA47) |
| `$0E` | `oam_step` | how far to advance oam_index per sprite ($DA5F) |
| `$0F` | `rng_state` | the random number generator's state |
| `$10` | `rng_stir` | a rolling index into zero page; rng mixes the byte it points at into the state, so the sequence is coupled to whatever the game is doing |
| `$11` | `ptr_lo` | \ general-purpose 16-bit pointer. Used as the menu's |
| `$12` | `ptr_hi` | / indirect jump vector ($CA66) AND as the terrain map pointer returned by coords_to_map ($D706). |
| `$13` | `str_ptr_lo` | \ pointer to an $FF-terminated string, consumed by |
| `$14` | `str_ptr_hi` | / draw_text_bricks ($D8D2). Table at $D299. Reused by draw_tiles ($D6B3) as the DESTINATION map pointer, with the source in $11/$12. |
| `$15` | `digits` | 6 x (7 digits + $FF) |
| `$45` | `shovel_timer` | Shovel powerup. Set to 20 and ticked down once every 64 frames (~21 s total) by shovel_and_base. In its last 4 units the wall alternates between concrete and brick every 16 frames -- the warning blink. ($E2A9) |
| `$46` | `game_mode` | three values: 0 = stages 1-35, 1 = stages 36-70 (second lap), 2 = attract demo. |
| `$47` | `cell_x` | draw_block: the block's cell X. bullet_vs_tank reuses the same byte as plain scratch for the bullet's owning player -- unrelated to cells. |
| `$48` | `cell_y` |  |
| `$49` | `unk_49` | loop index in powerup_pickup |
| `$4A` | `cheat_accum` | (guess) dial for the two-controller title cheat. Zeroed on menu entry; +$10 or -1 per frame; wants $74. |
| `$4B` | `construction_count` | counts exits from construction mode ($C154). Nonzero also means "a player-built level exists" ($C1D0). The cheat requires exactly 7. |
| `$4C` | `unk_4C` | (guess) stage-intro timer; nonzero ends the intro |
| `$4D` | `palette_request` | WHICH BACKGROUND PALETTE ROW TO UPLOAD NEXT NMI. $D416 tests it and skips on bit 7, so $FF means "nothing to do" -- and load_bg_palette ($D529) writes $FF itself after uploading. So it is a one-shot request, not a level: every frame that wants a palette change has to re-arm it. $03 blank/menu, $04 stage intro, 5..8 cycling on the hiscore screen -- and during play palette_cycle flips it between 2 and 1 every 32 frames ($C31D), which is the slow colour animation on the playfield. Bit 7 set makes the NMI skip the update entirely. |
| `$4F` | `scroll_y` | PPUSCROLL Y. Written as the SECOND $2005 write ($D429); the first write (X) is always 0. |
| `$50` | `nametable_sel` | ORed into PPUCTRL as `$B0 \| nametable_sel` ($D41F), so it supplies the base-nametable bits, not a scroll. 0 normally, 2 on the title menu. |
| `$51` | `player_lives` | lives remaining, indexed by player ($51 p1, $52 p2). Confirmed by `DEC $51,X` at $DE0D and `INC $51,X` in the Tank powerup. Both zero is one of stage_over's game-over conditions. draw_hud shows lives MINUS ONE. |
| `$53` | `unk_53` | base sprite tile. CONFIRMED session 14d: $B1 bullet, $29/$2D shield, $A1/$A5/$A9/$AD spawn twinkle, $3B score popup, $81+type*4 powerup icon, $F1/$F5/$F9 explosion, $D1/$E1 the 32x32 pair, $B9+kind*4 the floating 100/200/300/400. Every 16x16 steps by 4 because draw_sprite16_at emits two 8x16 sprites two tiles apart -- so $2B is the RIGHT HALF of shield frame 0, not a frame of its own. (That misreading stood here until session 16.) NOTE $53 also holds the string pointer's low byte in the title code -- reused scratch. |
| `$54` | `unk_54` | \|dx\| for a bullet's heading (perpendicular probe) |
| `$55` | `unk_55` | \|dy\| |
| `$56` | `tmp_x` | shared coordinate scratch pair. Two uses so far: |
| `$57` | `tmp_y` | draw_text_bricks' target (x,y), and the tank's candidate next position in move_tank, which the corner probes are measured against. |
| `$58` | `probe_dx8` | dx * 8 for the current direction, used to build the |
| `$59` | `probe_dy8` | dy * 8   two leading-corner probes in move_tank. |
| `$5A` | `slot_idx` | loop variable, and the most overloaded byte in the ROM -- see tally_row and cell_h below, plus draw_enemy_column ($C8C0), which counts it 18 -> 0 by twos. Every per-tank loop counts 7 down to 0 through this. load_level reuses it as the nibble counter while decoding level data -- same byte, an unrelated job, since the two never overlap. |
| `$5A` | `tally_row` | the stage-tally row (an enemy kind). Yet another reuse of the general loop index. |
| `$5A` | `cell_h` | and yet another: the HEIGHT, in cells, of the rectangle reset_terrain_map ($D7E9) clears. |
| `$5B` | `cell_w` | its WIDTH. clear_playfield ($C9B0) sets both to $1A, which with cell_x/cell_y = 2 is exactly the 13x13 blocks of playfield -- see TILE_HARD. |
| `$5B` | `unk_5B` | inner loop index (the bullet, in bullet_vs_player) |
| `$5C` | `edit_tile_sel` | construction mode: selected tile, 0..13 |
| `$5D` | `tally_count` | per-player count shown on the tally row |
| `$60` | `digit_tile_base` | added to every byte draw_hud_number emits ($D6F5), so a buffer of digit VALUES becomes digit TILES. $6E, the tile for "0", while a number is drawn; 0 the rest of the time. |
| `$62` | `powerup_flash` | the collect flash. Set to $32 when a powerup is picked up; draw_powerup counts it down and clears powerup_x when it reaches 0, so it is also the powerup's lifetime after collection. spawn_powerup reuses it as the return value of its placement test. |
| `$64` | `scratch_64` | AI scratch: sign(dx)+1, then the 3x3 table index |
| `$65` | `scratch_65` | AI scratch: sign(dy)+1 |
| `$66` | `extra_life_given` | latches the 20,000-point bonus life so each player can only earn it once per game ($D13E/$D153) |
| `$68` | `base_state` | the eagle. $80 = intact (set by start_stage), $27 = destroyed (set by hit_terrain at $E6AE). $C27A requires $80 to advance, so losing it ends the game even with lives left, as game_rules.md says. |
| `$6A` | `spawn_point_idx` | rotates 0,1,2 through the three enemy spawn points. Enemy arrival positions are a fixed rotation, not random. ($E37C) |
| `$6B` | `narrow_zero` | how align_number ($D942) handles a number that is all zeros: nonzero shows ONE "0", zero shows two. draw_hud sets it (lives can legitimately be 0); draw_hud_flag leaves it clear. |
| `$6C` | `unk_6C` | HIGHEST TANK SLOT IN USE. spawn_enemy searches slots $6C down to 2 for a free one, so this sets how many enemies can be on screen at once: 5 = 1 player  -> slots 5..2  = 4 enemies 7 = 2 players -> slots 7..2  = 6 enemies That is what tank slots 6 and 7 are for. ($DB53) |
| `$6D` | `paused` | 1 = paused. Also 1 for the whole attract demo, which still calls main_loop -- so the meaning that fits both is "player input frozen". (guess) |
| `$6E` | `behind_trees` | SPRITE-BEHIND-BACKGROUND bit, ORed into a sprite's attribute byte by draw_sprite ($DA41) -- but only when the terrain cell under that sprite is TILE_TREES. $20 is OAM attribute bit 5, "behind background", so this is literally game_rules.md's "trees: tanks are displayed under it". (Renamed from unk_6E, session 21. The behaviour was read in session 17; the name waited until the opt-out list below made the purpose unambiguous.) |
| `$6F` | `tank_freeze` | THE STUN. Per-player countdown ($6F p1, $70 p2); while nonzero the player's input is skipped entirely ($DB8B). Set to $C8 when the OTHER player shoots you ($E8AA) -- game_rules.md's "player shooting another player stuns them". Not in the demo. $C8 is 200 TICKS, NOT 200 frames: the countdown is inside player_control, which runs 3 frames in 4, so it lasts ~267 frames / 4.4 s. (Corrected session 20; same trap as the spawn twinkle in session 16.) Note it does NOT refresh -- $E8A0 skips a player who is already stunned, so you cannot chain-lock them. |
| `$71` | `seek_x` | AI target position. Set by the three seek handlers |
| `$72` | `seek_y` | to a player's position or to the eagle's ($78,$D8). |
| `$73` | `p1_kills` | per-enemy-kind kill counts for player 1, used by |
| `$77` | `p2_kills` | the end-of-stage tally. ($E80E / $E813) |
| `$7B` | `unk_7B` | (?) |
| `$7C` | `tally_ticked` | (?) set while a tally row is still counting |
| `$7D` | `p1_total_kills` | sum of p1_kills[], for the tally's TOTAL line |
| `$7E` | `p2_total_kills` | sum of p2_kills[] |
| `$7F` | `enemies_to_spawn` | enemies still to be released this stage. ALWAYS starts at 20 ($C353), every stage. |
| `$80` | `enemies_left` | enemies still alive. Seeded to 20 with enemies_to_spawn, and decremented when an enemy FINISHES exploding ($DE15). stage_over watches it for the stage-cleared condition. |
| `$81` | `edit_palette_up` | (?) construction "tile picker open" latch |
| `$82` | `spawn_cooldown` | frames until the next enemy is released. Reloaded from ai_clock after each spawn ($DB5D), so later stages spawn faster as well as playing meaner: ~3.1 s apart on stage 1, ~0.8 s on stage 35. |
| `$83` | `menu_sel` | title menu selection 0..2: 0 = 1 PLAYER, 1 = 2 PLAYERS, 2 = CONSTRUCTION |
| `$84` | `ai_clock` | enemy aggression clock for the stage: $BE - stage_number*4, minus $14 in a 2-player game pick_direction compares phase_timer against ai_clock/8 and ai_clock/4 to pick the AI mode, so a smaller value means enemies turn aggressive sooner. Stage 1 = $BA, stage 35 = $32. ($C39C) |
| `$85` | `stage_number` | current stage 1..70 (displayed 1..35) |
| `$86` | `unk_86` | (?) cleared at stage start and when a bonus enemy spawns -- plausibly "a powerup is on the field" |
| `$86` | `powerup_x` | the powerup on the field. 0 = none out, which is |
| `$87` | `powerup_y` | why start_stage and spawn_tank clear $86. |
| `$88` | `powerup_type` | index into tbl_powerup: 0 Helmet   1 Timer    2 Shovel 3 Star     4 Grenade  5 Tank (extra life) Bit 7 set means PROBE MODE -- powerup_pickup then registers the touch but does not award or dispatch, which is how spawn_powerup tests a candidate spot. |
| `$89` | `player_shield` | per-player SHIELD, in units of 64 frames -- draw_shield ($E28C) decrements one whenever frame_counter & $3F is 0. While nonzero, a bullet that hits the player is deleted with no effect ($E753). CONFIRMED session 16: init_tank_type sets it to 3 as the tank goes live ($E3C1), so every spawn carries ~2-3 s of invulnerability; the Helmet sets 10 ($E9F0). The countdown rides a global clock, not a per-player one, so the first unit is a partial one -- 3 units is 129-192 frames, not a flat 192. |
| `$8B` | `enemy_count` | remaining enemies of each type this stage, loaded from tbl_enemy_counts by load_enemy_counts ($E43C) |
| `$8F` | `enemy_type_idx` | which entry of enemy_count[] is being handed out; walks forward past exhausted types ($E3CB) |
| `$90` | `tank_x` | X coordinate      (user, confirmed) |
| `$98` | `tank_y` | Y coordinate      (user, confirmed) |
| `$A0` | `tank_state` | state + facing: bit 7      active. Cleared = slot unused; every loop skips on BPL. >= $E0     dying/exploding -- loops skip these too (bits 7,6,5 all set) bit 5      set while moving ($A0\|dir at $DBE7) bit 3      set when idle ($DBAB ORA #$08) bits 1-0   facing. dir ^ 2 is the opposite direction, which the turn code relies on. |
| `$A8` | `tank_type` | high nibble = tank type. $A0 = the fast type, which moves every frame instead of every other ($DC29). Low nibble unknown -- armour level for the armoured enemy would fit game_rules.md. (guess) |
| `$B0` | `tank_b0` | animation frame. The menu flips bit 2 every 4 frames to animate the cursor tank's treads. |
| `$B8` | `bullet_x` |  |
| `$C2` | `bullet_y` |  |
| `$CC` | `bullet_state` | high nibble = mode, low 2 bits = direction: $00  slot free $1x-$3x  hit animation, counting down $4x  in flight Cleared for all ten at stage start ($E409). |
| `$D6` | `bullet_flags` | bit 0 = fast    (steps twice a frame, 4 px) bit 1 = powerful (destroys concrete, and wipes a whole cell of brick instead of a quarter) Set from the firing tank's type in fire_bullet. |
| `$E0` | `tank_map_lo` | \ cached terrain-map pointer for this tank, recomputed |
| `$E8` | `tank_map_hi` | / every frame by update_tank_terrain ($E1A0). The high byte is masked to `& $03` and bits 7/6 are then reused as flags: bit 7  tank_x is 8-pixel aligned bit 6  tank_y is 8-pixel aligned So the real high byte must be OR'd back with the map's base page before use. |
| `$F0` | `snd_ptr_lo` |  |
| `$F1` | `snd_ptr_hi` |  |

## Stack page ($0100-$01FF)

| Addr | Symbol | Description |
|---|---|---|
| `$0100` | `enemy_freeze_timer` | the TIMER powerup. Decremented once every 64 frames ($DC00); while nonzero, every active enemy is skipped by move_all_tanks ($DC23). Players are unaffected. Matches game_rules.md "Timer - stops time for enemies for a moment". |
| `$0101` | `player_level` | per-player tank level, folded into tank_type by init_tank_type ($E3C5). game_rules.md: levels 1-4, gained from Star powerups. |
| `$0103` | `player_slide` | THE ICE SKID. $0103 p1 / $0104 p2, players only. bit 7    set while the cell under the tank is TILE_ICE, cleared otherwise ($E1B0) bit 4    while set the player's input is IGNORED that frame ($DB99), so the tank keeps its heading -- the skid itself bits 4-0 countdown; at 0 it resets to $9C and sound $0310 fires From $1C, bit 4 is set for 13 frames then clear for 16, so control stutters in and out. CONFIRMED by the user: $0103 changes on ice. |
| `$0105` | `banner_x` |  |
| `$0106` | `banner_y` | parked at $F0, below the screen |
| `$0107` | `banner_dir` | a direction 0..3, same encoding as tanks |
| `$0108` | `banner_state` | $11 centred / $0D per-player / 0 = not showing. This is also what makes the end-of-stage sequence start 4 states early at $C22F -- it means "a game over is being displayed". |
| `$0180` | `vram_queue` | -$01FF, 32 entries of 4 bytes |

## $0200 page

| Addr | Symbol | Description |
|---|---|---|
| `$0200` | `oam_buffer` |  |

## $0300 page

| Addr | Symbol | Description |
|---|---|---|
| `$0300` | `sound_req` | ..$031B -- 28, per sound_reset's loop $0300      pause flag mirror ($C218) $0301-03   set to 1 at stage start $0307      eagle destroyed ($E6B7) $030B      eagle destroyed ($E6B2) $0304-05   extra life ($EA40) $0306      powerup collected ($E9C5) $0309      powerup appeared ($E8BE) $030A      enemy destroyed ($E7F6) $030C      brick or concrete destroyed ($E6E3, $E6F5) $030D      bullet clangs off concrete ($E704) $030E      shot bounced off enemy armour ($E7EA) $030F      player shot ($E094) $0310      set to 1 by the player_slide cycle ($DBC4) $0311      tank engine: 1 = a player is driving $0312      set at stage start $0315-17   hiscore jingle; $0315 is polled for "done" |
| `$031C` | `sound_voices` | 28 x 8. sound_reset clears byte 0 of each, which is enough to silence them. |

## $0400+ 

| Addr | Symbol | Description |
|---|---|---|
| `$0400` | `terrain_map` |  |

## Hardware registers

| Addr | Symbol | Description |
|---|---|---|
| `$2000` | `PPUCTRL` |  |
| `$2001` | `PPUMASK` |  |
| `$2002` | `PPUSTATUS` |  |
| `$2003` | `OAMADDR` |  |
| `$2005` | `PPUSCROLL` | written twice: 1st = X, 2nd = Y |
| `$2006` | `PPUADDR` |  |
| `$2007` | `PPUDATA` |  |
| `$4014` | `OAMDMA` |  |
| `$4016` | `JOY1` |  |
| `$4017` | `JOY2` | ON READ. Writing $4017 hits the APU frame counter instead -- the same address is two different registers by direction, and sound_reset ($EA58) uses the write side. |
| `$CA69` | `tbl_menu_targets` | three little-endian addresses, indexed by menu_sel*2: $CA6F (1P), $CA74 (2P), $CA7E (build) |
| `$D36D` | `tbl_eagle_brick` | 4 rows of 6 cells + $FF |
| `$D389` | `tbl_eagle_concrete` | the same, with $10 instead of $0F |
| `$D3D1` | `tbl_tally_score` | 10 20 30 40 -- the same values as tbl_enemy_score, used again by the end-of-stage count-up |
| `$D3D5` | `tbl_banner_dx` | a THIRD copy, for the game over banner |
| `$D3D9` | `tbl_banner_dy` |  |
| `$D555` | `tbl_sprite_palette` | -> PPU $3F10 |
| `$D565` | `tbl_bg_palettes` | -> PPU $3F00, row = palette_request. Mirrored in battlecity/src/palettes.ts as indices into nes-palette.ts. |
| `$DABB` | `tbl_block_attr` | palette index per block type |
| `$DACB` | `tbl_block_cells` | the 2x2 cell pattern per block type Together these exactly fill the region Mesen marks as data, $DABB-$DB0A. |
| `$E003` | `tbl_enemy_palette` | 02 00 00 01 02 01 02 02 -- the sprite palette for a non-bonus enemy, indexed by (frame_counter * 4 + tank_type) & 7 ($DFCD). frame*4 mod 8 is 0 or 4, so each type picks between two entries four apart and flickers between them every frame. That is the armoured tank changing shade as its layers come off. |
| `$E46C` | `tbl_dx` | 00 FF 00 01 |
| `$E470` | `tbl_dy` | FF 00 01 00 |
| `$E474` | `enemy_spawn_x` | 18 78 D8   left / centre / right |
| `$E477` | `enemy_spawn_y` | 18 18 18   top |
| `$E47A` | `player_spawn_x` | 58 98      p1 / p2 |
| `$E47C` | `player_spawn_y` | D8 D8      bottom |
| `$E47E` | `tbl_slot_facing` | starting tank_state per slot: A0 A0 A2 A2 A2 A2 A2 A2 players face UP, enemies face DOWN ($E3B8) |
| `$E486` | `tbl_heading` | A0 A0 A0 A1 A0 A3 A2 A2 A2 A1 A0 A3 A1 A0 A3 A1 A2 A3 |
| `$E498` | `tbl_state_handlers` | move_tank's dispatch, indexed by (tank_state >> 4) * 2. See move_tank(). |
| `$E4B8` | `tbl_tank_draw` | draw_tank's dispatch, same index. The two tables run in parallel: a tank's mode picks both what it does and what it looks like. See draw_all_tanks() in sprites.c. |
| `$E4D8` | `tbl_bullet_move` | update_bullets dispatch, 5 entries |
| `$E4E2` | `tbl_bullet_draw` | draw_bullets dispatch, 5 entries. The two sit back to back and fill $E4D8-$E4EB exactly. |
| `$E4EC` | `tbl_enemy_type` | which kind each group is, per stage |
| `$E578` | `tbl_enemy_counts` | how many of each, per stage. Together these two run $E4EC-$E603 with no gap. |
| `$E8BA` | `tbl_enemy_score` | 10 20 30 40 -- BCD hundreds, so 100/200/300/400 points for basic/fast/power/armour |
| `$E8FA` | `tbl_powerup_kind` | 00 01 02 03 04 05 04 03 |
| `$E9E2` | `tbl_powerup` | effect handlers, indexed by powerup_type * 2: 0 $E9F0 Helmet   -> player_shield = $0A 1 $E9F5 Timer    -> enemy_freeze_timer = $0A 2 $E9FB Shovel   -> concrete wall + timer 3 $EA07 Star     -> player_level += $20, cap $60 4 $EA17 Grenade  -> kill every live enemy 5 $EA3E Tank     -> an extra life 6 $EA48 unused, a bare RTS -- and yet it has finished ART: the icon at tile $99 is a pistol, sitting right after the other six. tbl_powerup_kind ($E8FA) only ever draws 0-5, so nothing can spawn it. Session 16. |
| `$EA49` | `tbl_dx2` | a second copy of tbl_dx, used by bullet_vs_terrain |
| `$EA4D` | `tbl_dy2` | and of tbl_dy |

