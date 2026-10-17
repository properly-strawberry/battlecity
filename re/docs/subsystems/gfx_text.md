# gfx_text notes

All 21 routines lifted and passing (difftest summary `{'pass': 21}`; lockstep identical on all 7 movies).
Calls cells.c functions directly (cell_to_addr, map_ptr_from_cell, locate_pixel, write_cell, clear/set_quarter_quiet, write_cell_direct, ptr_advance, queue_block_palette) via extern prototypes.

## Names (observed unless marked)
- $D0D9 set_tally_attributes: writes attribute shadow $07C0-$07F7 ($50 x10, $A0 x4, $0A x6, $05 x6).
- $D138 check_extra_life: base_state==$80, score digit 2 (p1 digits+2, p2 digits+10) >= 2 => +1 life, extra_life_given latch, sound_req+4/+5 = 1.
- $D47E clear_terrain_map (zeroes $0400-$07FF).
- $D4E3 write_boot_signature / $D4EF boot_signature_valid (returns A=1 match, 0 mismatch; compares 16 bytes $0110 vs $C040).
- $D50E load_bg_palette(A=row): PPU $3F00 <- tbl_bg_palettes+row*16; palette_request=$FF; then 4 PPUADDR writes ($3F,$00,$00,$00) to park the address.
- $D53E load_sprite_palettes: PPU $3F10 <- 16 bytes at $D555.
- $D7B4 render_playfield: pushes $0400-$07FF to PPU with write_cell_direct (+ppu_page_offset page).
- $D7CC reset_terrain_map: fill shadow with $11 (border), zero attributes $07C0-$07FF, then clear cell_w columns x slot_idx rows at cell (tmp_x,tmp_y) to 0. (Inputs tmp_x/tmp_y here are CELL coords.)
- $D80B draw_block(A=type,X=px,Y=py): see block types below.
- $D85E draw_glyph_bricks(A=tile,X=px,Y=py): reads 8 pattern bytes of CHR $1000+tile*16 via PPUADDR/PPUDATA (dummy read first), pushes them on the CPU stack, then for each font pixel sets/clears one brick quarter in the map: a 32x32 px brick-letter. Scratch: tally_count($5D)=pixel X, $5E=pixel Y, $02=row byte, $03=bit mask, slot_idx=row count. Note $5D/$5E/$5F get reused as scratch here (tally_count name is misleading).
- $D8D2 draw_text_bricks: string at str_ptr($13), pixel pos tmp_x/tmp_y (tmp_x += $20 per char), adds digit_tile_base. $5F = string index.
- $D8FD flush_vram_queue. $D934 align_number. $D951 draw_hiscore_bricks (pixel (16,100), tile base $30).
- $D97D update_hiscore (Y: 0 none, 1 p1 record, $FF p2 record). $D9BE add_digits. $D9E1 set_score_popup. $D9FE clear_digits. $DA13 number_to_digits. $DA93 clear_oam (hide sprites).

## Digit buffers (`digits` $15, 8 bytes each: 7 digit values + $FF)
- buf0 $15 p1 score, buf1 $1D p2 score (init clears with clear_digits $15/$1D at $D4B2).
- buf2 $25 / buf3 $2D: p1 / p2 running tally-screen point totals (add_digits X=2 at $CD36, X=3 at $CD54). (derived from callers)
- buf4 $35: scratch number: popup points (set_score_popup) or number_to_digits output.
- buf5 $3D: hi-score (initialised to 0002000 -> digits+42=2 i.e. 20,000? at $D4C6 only when boot signature invalid; also menu_sel=0).
- Score = 7 decimal digits, most significant first; d2 = ten-thousands.
- set_score_popup(bcd): packed BCD in hundreds. digits[36]=high nibble, digits[37]=low nibble, last digit (digits[38]) stays 0, so $50 shows "500" (callers pass $50 at $E9B6 and $00 at $CE3C/$CE97); bcd==0 sets digits[35]=1 => "1000".
- number_to_digits writes tens to digits+37, units to digits+38 (tens may exceed 9 for n >= 100).

## Block types (tbl_block_attr $DABB palette, tbl_block_cells $DACB, cells TL,TR,BL,BR)
0 right-half brick, 1 bottom-half brick, 2 left-half brick, 3 top-half brick, 4 full brick (pal 0; tiles $00/$0F);
5-8 same halves in steel (tiles $10 / blank $20, pal 3), 9 full steel (pal 3);
10 water ($12, pal 1), 11 trees ($22, pal 2 - $22 is the tile draw_sprite_oam tests for "behind trees"), 12 ice ($21, pal 3), 13-15 empty (pal 0). Tile identities beyond $22 are derived from palette/usage.

## QUIRKs
- flush_vram_queue: data byte $FF is a terminator only if next byte != $FF; "FF FF" emits one literal $FF. A record whose hi byte is $FF can never be flushed.
- draw_glyph_bricks: tile > $80 reads tile 0's pattern (dex/bmi loop for the *16 offset).
- reset_terrain_map: cell_w == 0 (or > $81) stores a single byte at ptr+$FF.
- clear_oam negates oam_step in place every call (D4A6 sets 4, so it is $FC afterwards).
- check_extra_life: awarding p1 skips the p2 check for that call.
- update_hiscore compares via sign of 8-bit difference of first differing digit; p2 is compared against the already-updated hi-score.
- add_digits loses adc carry-out overflow (carry is rewritten by the >=10 test); fine for digits 0-9.
- draw_block leaves cell_x = type*4+4 (write_cell's X save slot).

## Not lifted / unreached
None; every routine was exercised (difftest 21 pass, no unreached).

## Unknowns
- Exact tile art for $10/$20/$21/$12; whether buf2/buf3 are cleared elsewhere.
- Hi-score default: $D4C6 sets digits+42 = 2 (digit index 2 of buf5 => 20,000) when boot signature invalid.
