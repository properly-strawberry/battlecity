# screens package notes

Method: read each routine in docs/battlecity.asm, wrote C, then checked with lockstep
(all 7 movies identical, 40000 frames) and a coverage run (build/nesrun --cov) for which routines any movie executes.
difftest: `summary: {'no-cases': 11}` (every routine waits, or has no captured entry state; nothing FAILED).

## Names (evidence)
- $D276 delay_frames (observed): `wait_frame; txa; pha; jsr $D0B8; pla; tax; dex; bne`: waits X frames (X=0 gives 256) and redraws the four tally tanks each frame.
- $D16A clear_screen (observed): rendering_off, palette_request=3, ppu_page_offset=$1C, clear_terrain_map, render_playfield, rendering_on.
- $D17F draw_title_logo (observed): ppu_page_offset=$24; draw_text_bricks of strings at $D299 "BATTLE" and $D2A0 "CITY" (bytes 42 41 54 54 4C 45 FF / 43 49 54 59 FF); then labels and score numbers on row 3, then menu strings ($D2C6 "1 PLAYER", $D2CF "2 PLAYERS", $D2EB "CONSTRUCTION", copyright text $D2F8/$D320), spread over three frames (wait_frame between batches so the 32-entry VRAM queue does not overflow: derived from the placement of the waits).
- $C859 draw_hud_flag (observed): flag tiles ($D365,$D368) at col 29 rows 23/24, stage_number with digit_tile_base=$6E at row 25.
- $CA91 stage_intro_text (observed): queues tiles $23..$27 + two $11 at cell (12,14) (the word STAGE; tile identity derived from tile numbers, not checked against the CHR), then stage number with small digit font. Called from $C172 per the earlier notes; coverage shows all gameplay movies run it.
- $CC27 upload_attributes (observed): copies the attribute shadow $07C0-$07FF to PPU $23C0.. one byte per frame, 64 frames.
- $CC5C queue_row (internal helper, not in the list; folded into the C as a static): queues one 32-tile row at PPU row y; if RAM[$63]==0 copies the shadow row, else fills with the tile in $63. It reproduces cell_to_addr's scratch_00 side effect ((row&7)<<5) and leaves ptr = shadow row address.
- $CC90 curtain_close / $CCB2 curtain_open (observed): rows (n, 29-n) per frame, n=0..15 up with $63=$11 (grey fill), n=15..0 down with $63=0 (restores the shadow = reveals the stage). 16 frames each. Called from $C16F and $C1EC.
- $CEF7 build_tally_screen, $CCD4 stage_tally (confirmed names from the earlier notes; behaviour verified in the code).
- $C5D9 game_over_screen (observed): strings $D343 "GAME" at (60,70) and $D348 "OVER" at (60,120).
- $C49C credits_screen, $C567 wait_64_frames, $C575 credits_sprite_drop, $C5B0 draw_credit_star: see below.
- RAM: $5E = P2 kills counted on the current tally row (mirror of tally_count $5D for P1; observed: inc $5E at $CD50, used at $CDBF). $63 = row fill tile for the curtain (0 = copy shadow). Both unnamed in ram.h; proposed `tally_count_p2` and `row_fill_tile`.

## Screen sequences and timing
Tally ($CCD4): build_tally_screen (about 5 waits, so roughly 6 frames plus rendering_off/on waits), delay 30, then per enemy kind (slot 0..3):
 wait, clear the two score-popup digit buffers (offsets $25,$2D), then loop: wait; set the popup to tbl_tally_score[slot] ($D3D1 = 10,20,30,40; packed BCD, split into two digits by $D9E1, so "10" etc. and the "PTS" text follows: derived); each pass removes ONE kill from p1_kills[slot] and one from p2_kills[slot] (if nonzero), counts them, adds the popup to the score buffers (add_digits X=2 for P1, 3 for P2; check_extra_life after each); redraws score and row numbers; delay 8 frames; repeat while any player ticked this pass. So one kill per player per 8+2 frames. After a row: delay 20 frames. After the 4 rows: delay 30, print total kills (row 23), delay 15.
 Bonus: only in 2-player mode (menu_sel!=0) and if the eagle survives (base_state!=0): the player with strictly more total kills (P1 wins if p2_total < p1_total, and has lives) gets set_score_popup(0)/add_digits (A=0, so a zero popup at digit buffer offset $35: the bonus text is the BCD from the digits left there; derived, see Unknowns). Draws $D3C4 and $D35E tiles at row 25/26 and plays sound_req[27] plus sound_voices[0..1]. QUIRK: ties give nothing. QUIRK: P1's check includes lives!=0, if P1 has zero lives the code falls to the P2 test, which then requires p1_total<p2_total.
 End: delay 120, clear nametable_sel, digit_tile_base, narrow_zero, palette_request.
Game over: black screen, text, jingle sound_req[24..26]=1, wait until joy_pressed[0]&$0C (Select/Start of P1) or sound_req[24] returns to 0.
Curtain: 16 frames close, intro text, 16 frames open (callers outside my package).

## Credits easter egg (lifted; reached by `fceux_easteregg` only)
$C49C is called only from $CA4F (title_menu): when Start is pressed (joy_pressed&8) with construction_count==7 and cheat_accum==$74 (code at $CA43-$CA4F: cmp #$07 / cmp #$74). It draws, one every 64 frames ($C567 waits until frame_counter&$3F==0 after zeroing it): $D30F text, "WRITTEN BY" ($D284, bytes 57 52 49 54 54 45 4E 20 42 59), $D334 "OPEN-REACH", $D34D "WHO LOVES NORIKO" (the text bytes read from the table), then tile $69 at x=8..12 row 16, then a 16x16 sprite animation ($C575/$C5B0) falling to y=$F8, then clean-up. How the cheat_accum==$74 is built (two-controller input) is outside this package (the accumulation code is in title_menu). The C was written by hand-checking against the listing. Status (observed): all four routines are in `screens.lifted`. they pass the frame-aware difftest (2026-10-15; before that the harness could not run routines that wait) and lockstep is identical over `fceux_easteregg` (2284 NMIs, user recording), the only movie whose coverage (`nesrun --cov`) shows $C49C, $C567, $C575 and $C5B0 executed.

