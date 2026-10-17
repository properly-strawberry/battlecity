# sprites package notes

Result: 24 routines lifted and verified (difftest `{'pass': 24}`, lockstep OK on all 7 movies).
Listed since 2026-10-15: E0E6, E0D8 (difftest with the bullet_state < $50 invariant; see below).

## How the OAM system works (observed from $DA2B/$DA93)
- `draw_sprite_oam` ($DA2B) writes 4 bytes at `$0200+oam_index` (Y, tile, attr, X) then `oam_index += oam_step`.
  Evidence: `ldx $0D; sta $0200,x; sta $0201,x; ... adc $0E; sta $0D`.
- `$DA93` (NMI, not mine) negates oam_step each frame (eor #$FF, +1) and wipes slots with $F0 walking
  back by oam_step to index 4. So draw order alternates ascending/descending frame to frame => the flicker order.
  I reproduce oam_index/oam_step arithmetic exactly (uint8 wrap; writes at `$200+i+k` int arithmetic, so i>=$FD spills into $0300 like abs,X).
- The game uses 8x16 sprites (derived): every tile number is odd/even in pairs 2 apart and a "16x16 object" is two
  sprites with tiles T and T+2 (`inc unk_53` twice between the two `draw_sprite_oam` calls in $DA7B).
  Pattern-table bit = tile bit 0 (hardware rule for 8x16). Not checked in CHR by me.
- 16x16 object (`draw_sprite16_at` $DA7B, in X=x, Y=y): left sprite at x-8, right at x, both OAM Y = y-8 (observed: `sbc #8` on Y, `sbc #8` on X for the left half).
- Attribute byte = palette number only (scratch_04 0..3) | behind_trees($20) when the map cell is tile $22 (trees).
  `behind_trees` ($6E) is $20 normally and set 0 by explosions/banners/powerup so these draw in front.
- QUIRK: the map lookup for the trees test uses `coords_to_map(x+3, y)` with the UNshifted y: the original does
  `tya; sbc #8; sta cell_y` but leaves the Y register as y when it calls $D706. Found by difftest (scratch_00 mismatch
  of a factor 2 = one tile row). Consequence: the sprite is tested one row lower than where it is drawn.
- Side effects kept: cell_x=x, cell_y=y-8, ptr($11/$12), scratch_00 (from cell_to_addr), $54/$55 (x,y saved by $DA7B).

## Sprite/tile/palette layout (observed from the code; colours not checked)
| Object | tile (8x16 pair base) | palette (attr) | routine |
|---|---|---|---|
| Player tank | (tank_type&$F0) + tank_tread (0/4 tread frame) + facing*8 | slot number: P1=0, P2=1 | DFB6 |
| Enemy tank | same | tbl_enemy_palette[(frame*4+type)&7] = `02 00 00 01 02 01 02 02` ($E003) so it flickers between 2 palettes by type; bonus (type bit 2) alternates 2/3 every 8 frames | DFB6 |
| Spawn twinkle | $A1,$A5,$A9,$AD by n=\|state&$F - 7\| (tile=$A1+((2n)&$FC)) | 3 | E00B |
| Explosion small/med/large | $F1 / $F5 / $F9 | 3 | DEF0/DEE2/DECD/DF33 |
| Big 32x32 explosion | 4 quadrants, tile = (((state&$F0)-$30)^$10) + $D1 + q*4 | 3 | DF46/DF99 |
| Score popup (death tail) | $B9 + 4*kind (kind 0..3 = 100/200/300/400) | 3 | DEFD |
| Bullet | $B1 + facing*2 (single 8x16), drawn at x-5 | 2 | E0FB |
| Bullet hit puff | explosion tiles from state+$40 | 3 | E112 |
| Shield | $29 / $2D (flips on frame_counter bit 1) | 2 | E27C |
| Powerup icon | $81 + powerup_type*4, blinking (frame bit 3) | 2 | E23B |
| Powerup collected popup | $3B | 2 | E23B |
| PAUSE banner | 5 sprites tiles $17 $19 $1B $1D $1F at x=$64,$6C,$74,$7C,$84 y=$80 | 3 | C8F9 |
| GAME OVER banner | $79 at (game_over_x,y), $7D at (game_over_x+$10,y) | 3 | C947 |
| Tally tanks | $80,$A0,$C0,$E0 at x=$81, y=$64,$7C,$94,$AC | 2 | D0B8/D130 |

## Names (evidence)
- $DA2B draw_sprite_oam, $DA64 draw_sprite, $DA73 draw_sprite16, $DA7B draw_sprite16_at, $DEA6 draw_all_tanks, $DEB8 draw_tank, $DECD draw_explosion,
  $DEE2 draw_explosion_sprite, $DEF0 draw_explosion_frame, $DEFD draw_death_tail, $DF33 draw_explosion_last, $DF46 draw_explosion_big, $DF99 big_explosion_tile,
  $DFB6 draw_tank_body, $E00B draw_spawn_twinkle, $E0FB draw_bullet_sprite, $E112 draw_bullet_explosion, $E0E6 draw_bullet, $E0D8 draw_bullets,
  $E23B draw_powerup, $E27C draw_shield, $C8F9 draw_pause_banner, $C947 draw_game_over_banner, $C972 game_over_slide, $D0B8 draw_tally_sprites, $D130 draw_tally_tank.
  All names are the prior names, confirmed by reading the code (observed). Evidence for each is the tile constants and OAM writes in the table above.
- RAM: $53 = sprite_tile (written before every draw, base tile; observed), $04 = sprite_attr (observed: stored at oam+2),
  $86 = powerup_x (used as X of the icon `ldx $86` at $E26C, 0 = no powerup), $54/$55 = x/y save slots in $DA7B (observed).
  Others (powerup_y $87, banner_*, behind_trees) already named in ram.h.

## Restructurings / reasoning
- Jump tables: draw_tank ($E4B8) and draw_bullet ($E4E2) read the target address from ROM, store it into ptr_lo/ptr_hi ($11/$12)
  (the original does, as a side effect, before `jmp (ptr_lo)`), then `switch` on the 16-bit target. Target $DBF0 is a bare `rts`.
  Table by state high nibble: 0 nothing, 1 death_tail, 2 explosion_last, 3-4 big explosion, 5-7 explosion, 8-D tank_body, E-F twinkle.
- Targets are called directly (all in my list).
- draw_explosion_big reads the slot from `slot_idx` ($5A), not X (DF99 does `ldx slot_idx`). QUIRK; works because draw_all_tanks keeps slot_idx = slot.
- Loops use slot_idx in RAM as the original (final values: draw_all_tanks 8, draw_shield $FF, draw_bullets $FF) since it is observable and callees read it.
- Per-slot arrays use zp,X wrap (ZPX macro) and operands are read BEFORE sprite_attr/sprite_tile are stored (E0FB, E27C) because a fuzzed slot can alias $04/$53. Found by difftest.
- draw_explosion_sprite frame: (7 - hi_nibble)*4, 8-bit; hit states of a bullet are shifted by +$40 to reuse it. Death tail kind: ((type>>3)&$FC)-$10, equals (hi_nibble/2 - 4)*4.
- draw_shield QUIRK: draws even for a dead/exploding tank while player_shield != 0 (no state check). Observed in code.
- draw_powerup: when the pickup flash ends it clears powerup_x ($86) = 0, which removes the powerup.

## Not lifted
- (Resolved 2026-10-15: listed in sprites.lifted; difftest restricts fuzz to bullet_state < $50, a game-wide invariant.)
  $E0E6 draw_bullet and $E0D8 draw_bullets: C code written (in sprites.c, no adapters) and agrees on every real captured state and
  all in-domain fuzz, but difftest FAILs on fuzzed bullet_state with high nibble >= 5: the original indexes past the 5-entry table
  into $E4EC.. (bytes `80 A0 C0 E0`...) and jumps into the middle of ROM routines ($A080 etc.), which cannot be reproduced.
  Real bullet_state is always $00-$4F, so it never happens in play, but the guide requires all-pass, so these two stay mechanical
  (they still call my lifted $E0FB/$E112). The coordinator may accept them if the fuzz domain is restricted.
- $DBF0 (the `rts` table target) not mine.

## Unknowns
- Real colours of palettes 0-3 (not examined). 8x16 mode and tile-table choice derived from tile numbering, not checked against PPUCTRL bit 5 write.
- death_tail kind range for tank_type values other than $80/$A0/$C0/$E0 (low nibble nonzero) just follows the formula.
- Twinkle state low nibble 15 would give n=8 -> tile $B1 (probably never occurs).
