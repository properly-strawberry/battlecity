/* Full-screen sequences that wait for frames: stage tally, game over,
 * stage intro curtain, title logo, credits easter egg, frame delays.
 *
 * Common model: every "screen" is built with the PPU either off
 * (rendering_off/rendering_on bracket) or on, in which case drawing goes
 * through the VRAM queue and the code waits a frame between batches so the
 * queue (32 entries) cannot overflow. wait_frame ($D8F6) spins until the
 * NMI bumps frame_counter, so each call below is exactly one video frame.
 * Text/numbers are drawn with the mechanical helpers draw_tiles ($D6B3),
 * align_number ($D934), draw_hud_number ($D6DD): they belong to other
 * packages and are called directly. */
#include "common.h"

#include "lifted.h"

/* ---- helpers over the real functions of other packages ---- */
/* run of tiles from ROM address src (ptr) at cell (x,y) */
static void draw_tiles_at(uint16_t src, uint8_t x, uint8_t y) {
    ptr_hi = (uint8_t)(src >> 8); ptr_lo = (uint8_t)src;
    draw_tiles(x, y);
}
static void draw_text_str(uint8_t x, uint8_t y, uint16_t str) {
    tmp_x = x; tmp_y = y;
    str_ptr_hi = (uint8_t)(str >> 8); str_ptr_lo = (uint8_t)str;
    draw_text_bricks();
}
static void vq_put(uint8_t *x, uint8_t v) { RAM[0x180 + *x] = v; (*x)++; }

/* $D276 delay_frames
 * Wait n frames (n=0 means 256), redrawing the four tally tanks each frame
 * so the sprites survive the OAM rebuild. Used all over the tally. */
void delay_frames(uint8_t n) {
    do {
        wait_frame();
        draw_tally_sprites();
    } while (--n != 0);
}
void sub_D276(void) { delay_frames(X); X = 0; }

/* $D16A clear_screen: blank the nametable (palette row 3, screen $1C) */
void clear_screen(void) {
    rendering_off();
    palette_request = 3;
    ppu_page_offset = 0x1C;
    clear_terrain_map();
    render_playfield();
    rendering_on();
}
void sub_D16A(void) { clear_screen(); }

/* $D17F draw_title_logo
 * Title screen on nametable 1 ($24): BATTLE / CITY in brick letters, then
 * "I-" and "HI-" score rows (1P always, 2P when menu_sel != 0), the menu
 * texts "1 PLAYER", "2 PLAYERS", "CONSTRUCTION", the Namco copyright
 * lines. The last three batches are spread over frames to keep the VRAM
 * queue from overflowing (wait before each). */
void draw_title_logo(void) {
    rendering_off();
    ppu_page_offset = 0x24;
    clear_terrain_map();
    draw_text_str(0x1A, 0x2E, 0xD299);      /* "BATTLE" */
    draw_text_str(0x3C, 0x56, 0xD2A0);      /* "CITY" */
    render_playfield();
    rendering_on();
    digit_tile_base = 0x30;
    draw_tiles_at(0xD2A5, 2, 3);               /* "I-" label */
    draw_hud_number(align_number(4, 0x16), 3);                        /* player 1 score */
    draw_tiles_at(0xD2B1, 0x0B, 3);            /* "HI-" label */
    draw_hud_number(align_number(0x0E, 0x3E), 3);                        /* hi-score */
    if (menu_sel != 0) {
        draw_tiles_at(0xD2A8, 0x15, 3);        /* "II-" label */
        draw_hud_number(align_number(0x17, 0x1E), 3);                    /* player 2 score */
    }
    digit_tile_base = 0;
    wait_frame();
    draw_tiles_at(0xD2C6, 0x0B, 0x11);         /* "1 PLAYER" */
    draw_tiles_at(0xD2CF, 0x0B, 0x13);         /* "2 PLAYERS" */
    draw_tiles_at(0xD2EB, 0x0B, 0x15);         /* "CONSTRUCTION" */
    wait_frame();
    draw_tiles_at(0xD28F, 0x0B, 0x17);
    draw_tiles_at(0xD2F8, 4, 0x19);            /* copyright text */
    wait_frame();
    draw_tiles_at(0xD320, 6, 0x1B);
}
void sub_D17F(void) { draw_title_logo(); }

/* $C859 draw_hud_flag: stage flag at (29,23)-(30,24) and the stage number
 * on row 25 (tile base $6E gives the small HUD digits). */
void draw_hud_flag(void) {
    wait_frame();
    draw_tiles_at(0xD365, 0x1D, 0x17);
    draw_tiles_at(0xD368, 0x1D, 0x18);
    digit_tile_base = 0x6E;
    number_to_digits(stage_number);
    draw_hud_number(align_number(0x19, 0x36), 0x19);
    digit_tile_base = 0;
}
void sub_C859(void) { draw_hud_flag(); }

/* $C567 wait_64_frames: zero frame_counter and wait until its low 6 bits
 * wrap, i.e. 64 frames (about 1.07 s). */
void wait_64_frames(void) {
    frame_counter = 0;
    do {
        wait_frame();
    } while ((frame_counter & 0x3F) != 0);
}
void sub_C567(void) { wait_64_frames(); }

/* $C5B0 draw_credit_star: one frame, draw the 16x16 sprite at
 * (tmp_x,tmp_y). Its tile $A1+4*(3-|3-slot_idx|) makes a 4-frame
 * animation that peaks at slot 3 (palette 3). */
void draw_credit_star(void) {
    wait_frame();
    scratch_04 = 3;
    uint8_t a = (uint8_t)(3 - slot_idx);
    if (a & 0x80) a = (uint8_t)(~a + 1);
    scratch_00 = a;
    sprite_tile = (uint8_t)(((3 - scratch_00) << 2) + 0xA1);
    draw_sprite16_at(tmp_x, tmp_y);
}
void sub_C5B0(void) { draw_credit_star(); }

/* $C575 credits_sprite_drop: end of the credits. A 16x16 sprite at x=$78,
 * y=$1E animates for 7 x 4 frames (draw_credit_star, slot_idx 0..6), then
 * falls one pixel per frame (tile $9D, palette 1) until y=$F8. */
void credits_sprite_drop(void) {
    tmp_x = 0x78;
    tmp_y = 0x1E;
    slot_idx = 0;
    do {
        draw_credit_star(); draw_credit_star(); draw_credit_star(); draw_credit_star();
        slot_idx++;
    } while (slot_idx != 7);
    do {
        wait_frame();
        tmp_y++;
        sprite_tile = 0x9D;
        scratch_04 = 1;
        draw_sprite16_at(tmp_x, tmp_y);
    } while (tmp_y != 0xF8);
}
void sub_C575(void) { credits_sprite_drop(); }

/* $C49C credits_screen: payload of the hidden two-controller cheat
 * (reached from the title menu when Start is pressed with
 * construction_count == 7 and cheat_accum == $74). Shows a black screen
 * and types out, one line per 64 frames: "WRITTEN BY" / tiles, "WHO LOVES
 * NORIKO", ... then an animated sprite. Finally restores a clean playfield. */
void credits_screen(void) {
    rendering_off();
    ppu_page_offset = 0x1C;
    scroll_y = 0;
    nametable_sel = 0;
    clear_terrain_map();
    render_playfield();
    rendering_on();
    wait_64_frames();
    wait_64_frames();
    draw_tiles_at(0xD30F, 8, 8);
    wait_64_frames();
    draw_tiles_at(0xD284, 8, 0x0A);
    wait_64_frames();
    draw_tiles_at(0xD334, 8, 0x0C);
    wait_64_frames();
    draw_tiles_at(0xD34D, 8, 0x0E);
    wait_64_frames();
    for (uint8_t x = 8; x <= 0x0C; x++) {   /* single tile $69 drawn at x = 8..12, row 16 */
        draw_tiles_at(0xD33F, x, 0x10);
        if (x != 0x0C) wait_64_frames();
    }
    credits_sprite_drop();
    rendering_off();
    clear_playfield();
    render_playfield();
    rendering_on();
}
void sub_C49C(void) { credits_screen(); }

/* $C5D9 game_over_screen
 * "GAME" at (60,70) and "OVER" at (60,120) in brick text on a black
 * screen, jingle on sound_req[24..26]. Ends when the jingle finishes
 * (sound_req[24] returns to 0) or a player presses Start/Select
 * (joy_pressed[0] & $0C: P1 Select or Start; only player 1's pad is read).
 * Then clears the screen and re-arms the sound engine. */
void game_over_screen(void) {
    rendering_off();
    ppu_page_offset = 0x1C;
    scroll_y = 0;
    nametable_sel = 0;
    clear_terrain_map();
    draw_text_str(0x3C, 0x46, 0xD343);   /* "GAME" */
    draw_text_str(0x3C, 0x78, 0xD348);   /* "OVER" */
    render_playfield();
    rendering_on();
    phase_timer = 0;
    sound_req[24] = 1; sound_req[25] = 1; sound_req[26] = 1;
    for (;;) {
        wait_frame();
        if (joy_pressed[0] & 0x0C) break;
        if (sound_req[24] == 0) break;
    }
    rendering_off();
    clear_terrain_map();
    render_playfield();
    rendering_on();
    sound_reset();
}
void sub_C5D9(void) { game_over_screen(); }

/* $CA91 stage_intro_text: one frame after the curtain opens, queue the
 * "STAGE" label (tiles $23..$27 at cell (12,14)) with two blank tiles $11,
 * then the stage number with the small digit font (tile base $6E). */
void stage_intro_text(void) {
    wait_frame();
    uint8_t lo;
    uint8_t hi = cell_to_addr(0x0C, 0x0E, &lo);   /* $D5FB */
    uint8_t q = vram_queue_len;
    vq_put(&q, (uint8_t)(hi + 0x1C));
    vq_put(&q, lo);
    vq_put(&q, 0x23); vq_put(&q, 0x24); vq_put(&q, 0x25);
    vq_put(&q, 0x26); vq_put(&q, 0x27);
    vq_put(&q, 0x11); vq_put(&q, 0x11);
    vq_put(&q, 0xFF);
    vram_queue_len = q;
    digit_tile_base = 0x6E;
    number_to_digits(stage_number);
    draw_hud_number(align_number(0x0E, 0x36), 0x0E);
    digit_tile_base = 0;
}
void sub_CA91(void) { stage_intro_text(); }

/* $CC27 upload_attributes: copy the attribute shadow $07C0-$07FF to PPU
 * $23C0-$23FF, one byte per frame (64 frames), via the VRAM queue. */
void upload_attributes(void) {
    uint8_t y = 0;
    ptr_hi = 0x23;
    ptr_lo = 0xC0;
    do {
        wait_frame();
        uint8_t q = vram_queue_len;
        vq_put(&q, ptr_hi);
        vq_put(&q, ptr_lo);
        vq_put(&q, RAM[0x7C0 + y]);
        vq_put(&q, 0xFF);
        vram_queue_len = q;
        y++;
        set_zptr(0x11, (uint16_t)(ZPTR(0x11) + 1));
    } while (y != 0x40);
}
void sub_CC27(void) { upload_attributes(); }

/* $CC5C queue_row: queue one 32-tile nametable row y: either a copy of the
 * shadow row (when $63 == 0) or filled with the tile in $63. Leaves
 * scratch_00 = (y&7)<<5 and ptr = shadow row address like cell_to_addr. */
static void queue_row(uint8_t row) {
    scratch_00 = (uint8_t)((row & 7) << 5);
    uint8_t lo = scratch_00;                          /* col 0 | row bits */
    uint8_t hi = (uint8_t)((row >> 3) | 0x04);
    ptr_hi = hi; ptr_lo = lo;
    uint8_t x = vram_queue_len;
    vq_put(&x, (uint8_t)(ptr_hi + 0x1C));
    vq_put(&x, ptr_lo);
    for (uint8_t y = 0; y != 0x20; y++) {
        uint8_t v = row_fill_tile;
        if (v == 0) v = ind_y(0x11, y);
        vq_put(&x, v);
    }
    vq_put(&x, 0xFF);
    vram_queue_len = x;
}

/* $CC90 curtain_close: stage-intro curtain closing. $63 = $11 (grey tile).
 * Each frame fills the row pair (n, 29-n) for n = 0..15: grey bars from
 * top and bottom meet in the middle. 16 frames. */
void curtain_close(void) {
    row_fill_tile = 0x11;
    tmp_y = 0;
    do {
        wait_frame();
        queue_row(tmp_y);
        queue_row((uint8_t)(0x1D - tmp_y));
        tmp_y++;
    } while (tmp_y != 0x10);
}
void sub_CC90(void) { curtain_close(); }

/* $CCB2 curtain_open: reverse: restore the shadow rows (n, 29-n) from
 * n = 15 down to 0 ($63 = 0 means copy), 16 frames, revealing the stage. */
void curtain_open(void) {
    row_fill_tile = 0;
    tmp_y = 0x0F;
    do {
        wait_frame();
        queue_row(tmp_y);
        queue_row((uint8_t)(0x1D - tmp_y));
        tmp_y--;
    } while (tmp_y != 0xFF);
}
void sub_CCB2(void) { curtain_open(); }

/* $CEF7 build_tally_screen: draw the static tally layout on nametable 1
 * ($2400, ppu_page_offset $24, nametable_sel 2 so PPUCTRL selects it), PPU off
 * for the first part and then on with frame breaks in between. */
void build_tally_screen(void) {
    wait_frame();
    narrow_zero = 1;
    ppu_page_offset = 0x24;
    scroll_y = 0;
    nametable_sel = 2;
    digit_tile_base = 0x30;
    palette_request = 3;
    rendering_off();
    clear_terrain_map();
    set_tally_attributes();
    render_playfield();
    rendering_on();
    draw_tiles_at(0xD2BD, 8, 3);                /* "HI-SCORE" */
    draw_hud_number(align_number(0x12, 0x3E), 3);
    draw_tiles_at(0xD3CB, 0x0C, 5);             /* "STAGE" */
    number_to_digits(stage_number);
    draw_hud_number(align_number(0x0E, 0x36), 5);
    wait_frame();
    draw_tiles_at(0xD2D9, 3, 7);                /* "I-PLAYER" */
    draw_hud_number(align_number(5, 0x16), 9);
    draw_tiles_at(0xD3B1, 0x0E, 0x0C);          /* arrow + "PTS" row 1 */
    draw_tiles_at(0xD3B1, 0x0E, 0x0F);
    draw_tiles_at(0xD3B1, 0x0E, 0x12);
    draw_tiles_at(0xD3B1, 0x0E, 0x15);
    if (menu_sel != 0) {
        wait_frame();
        draw_tiles_at(0xD2E2, 0x15, 7);         /* "II-PLAYER" */
        draw_hud_number(align_number(0x17, 0x1E), 9);
        draw_tiles_at(0xD3B3, 0x11, 0x0C);
        draw_tiles_at(0xD3B3, 0x11, 0x0F);
        draw_tiles_at(0xD3B3, 0x11, 0x12);
        draw_tiles_at(0xD3B3, 0x11, 0x15);
    }
    wait_frame();
    draw_tiles_at(0xD35E, 8, 0x0C);
    draw_tiles_at(0xD35E, 8, 0x0F);
    draw_tiles_at(0xD35E, 8, 0x12);
    draw_tiles_at(0xD35E, 8, 0x15);
    if (menu_sel != 0) {
        wait_frame();
        draw_tiles_at(0xD35E, 0x1A, 0x0C);
        draw_tiles_at(0xD35E, 0x1A, 0x0F);
        draw_tiles_at(0xD35E, 0x1A, 0x12);
        draw_tiles_at(0xD35E, 0x1A, 0x15);
    }
    wait_frame();
    draw_tiles_at(0xD3BB, 0x0C, 0x16);          /* underline */
    draw_tiles_at(0xD3B5, 6, 0x17);             /* "TOTAL" */
}
void sub_CEF7(void) { build_tally_screen(); }

/* $CCD4 stage_tally: end-of-stage count-up (see docs/subsystems/screens.md). */
void stage_tally(void) {
    build_tally_screen();
    delay_frames(0x1E);
    p1_total_kills = (uint8_t)(p1_kills[0] + p1_kills[1] + p1_kills[2] + p1_kills[3]);
    p2_total_kills = (uint8_t)(p2_kills[0] + p2_kills[1] + p2_kills[2] + p2_kills[3]);
    slot_idx = 0;
    for (;;) {                                   /* one row per enemy kind */
        wait_frame();
        draw_tally_sprites();
        clear_digits(0x25);
        clear_digits(0x2D);
        tally_count = 0;
        tally_count_p2 = 0;                           /* p2 count for this row */
        do {                                     /* one kill per iteration */
            wait_frame();
            draw_tally_sprites();
            tally_ticked = 0;
            set_score_popup(ROM8(0xD3D1 + slot_idx));   /* 10/20/30/40 hundreds? see notes */
            if (p1_kills[slot_idx] != 0) {
                sound_req[19] = 1; sound_req[20] = 1;          /* tick sound */
                p1_kills[slot_idx]--;
                tally_count++;
                add_digits(2);
                tally_ticked = 1;
                check_extra_life();
            }
            if (p2_kills[slot_idx] != 0) {
                sound_req[19] = 1; sound_req[20] = 1;
                p2_kills[slot_idx]--;
                tally_count_p2++;
                add_digits(3);
                tally_ticked = 1;
                check_extra_life();
            }
            uint8_t row = (uint8_t)(slot_idx * 3 + 0x0C);
            draw_hud_number(align_number(5, 0x16), 9);                       /* P1 score */
            draw_hud_number(align_number(1, 0x26), row);                     /* P1 points for this row */
            number_to_digits(tally_count);
            draw_hud_number(align_number(8, 0x36), row);                     /* P1 kill count */
            if (menu_sel != 0) {
                draw_hud_number(align_number(0x17, 0x1E), 9);                   /* P2 score */
                draw_hud_number(align_number(0x13, 0x2E), row);
                number_to_digits(tally_count_p2);
                draw_hud_number(align_number(0x0E, 0x36), row);
            }
            delay_frames(8);
        } while (tally_ticked != 0);
        slot_idx++;
        if (slot_idx == 4) break;
        delay_frames(0x14);
    }
    delay_frames(0x1E);
    number_to_digits(p1_total_kills);
    draw_hud_number(align_number(8, 0x36), 0x17);
    if (menu_sel != 0) {
        number_to_digits(p2_total_kills);
        draw_hud_number(align_number(0x0E, 0x36), 0x17);
    }
    delay_frames(0x0F);
    /* bonus: 10000 points ("set_score_popup(0)" = zero hundreds + ... see notes)
     * to the player with more kills, if the eagle survived and he is alive */
    if (menu_sel != 0 && base_state != 0) {
        int p1_wins = !(p2_total_kills >= p1_total_kills) && player_lives[0] != 0;
        if (p1_wins) {
            set_score_popup(0);
            add_digits(0);
            draw_hud_number(align_number(5, 0x16), 9);
            draw_hud_number(align_number(1, 0x36), 0x1A);
            draw_tiles_at(0xD3C4, 3, 0x19);
            draw_tiles_at(0xD35E, 8, 0x1A);
            sound_req[27] = 1; sound_voices[0] = 1; sound_voices[1] = 1;
            check_extra_life();
        } else if (p1_total_kills < p2_total_kills && player_lives[1] != 0) {
            set_score_popup(0);
            add_digits(1);
            draw_hud_number(align_number(0x17, 0x1E), 9);
            draw_hud_number(align_number(0x14, 0x36), 0x1A);
            draw_tiles_at(0xD3C4, 0x16, 0x19);
            draw_tiles_at(0xD35E, 0x1B, 0x1A);
            sound_req[27] = 1; sound_voices[0] = 1; sound_voices[1] = 1;
            check_extra_life();
        }
    }
    delay_frames(0x78);
    nametable_sel = 0;
    digit_tile_base = 0;
    narrow_zero = 0;
    palette_request = 0;
}
void sub_CCD4(void) { stage_tally(); }
