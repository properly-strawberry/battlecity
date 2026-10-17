/* Top-level program flow: reset, NMI, frame waits, title / menu / attract
 * demo, construction mode, stage intro, the in-stage loop, stage end, game
 * over, stage start and level loading.
 *
 * The original links its screens with JMPs and discards return addresses
 * (`pla pla` in $C41D, $C7AB, $C9C0) to escape nested calls. Here that
 * becomes one state machine (game_main) with explicit transitions; the
 * comments carry the original addresses of every state. Routines owned by
 * other packages are called directly through their C functions. */
#include "common.h"
#include "lifted.h"

/* ------------------------------------------------------------------ */
/* frame synchronisation                                               */

/* $D8F6 wait_frame: wait until the NMI has advanced frame_counter. With
 * NMI disabled this never returns (as in the original). */
void wait_frame(void) {
    uint8_t f = frame_counter;
    do hal_wait_vblank(); while (frame_counter == f);
}
void sub_D8F6(void) { uint8_t f = frame_counter; wait_frame(); A = f; }   /* A = the old counter, as cmp leaves it */

/* $D5F5 wait for the PPU vblank flag by polling PPUSTATUS (used while NMI
 * is disabled). */
void wait_vblank_flag(void) {
    while (!(hal_read(0x2002) & 0x80)) hal_wait_vblank();
}
void sub_D5F5(void) { wait_vblank_flag(); }

/* $D467 rendering_on: at the next vblank, enable NMI (PPUCTRL = $B0: NMI on,
 * 8x16 sprites, background tiles at $1000). PPUMASK is set by the NMI. */
void rendering_on(void) { wait_vblank_flag(); hal_write(0x2000, 0xB0); }
void sub_D467(void) { rendering_on(); A = 0xB0; }

/* $D470 rendering_off: after the next frame, disable NMI and rendering so
 * the main thread may write VRAM directly. */
void rendering_off(void) { wait_frame(); hal_write(0x2000, 0x10); hal_write(0x2001, 0x06); }
void sub_D470(void) { rendering_off(); A = 0x06; }

/* ------------------------------------------------------------------ */
/* NMI                                                                 */

/* $D400 nmi: sprite DMA from $0200, flush the VRAM queue, upload a
 * requested background palette, set PPUCTRL/scroll/PPUMASK, read the pads,
 * run $DA93 and the sound driver, advance the frame counters
 * (frame_counter every frame, phase_timer every 64 frames).
 * The main thread's registers are preserved (the original pushes them). */
void nmi(void) {
    uint8_t sa = A, sx = X, sy = Y, sc = fC, sz = fZ, sn = fN, sv = fV;
    hal_write(0x2003, 0x00);
    hal_write(0x4014, 0x02);
    hal_read(0x2002);
    flush_vram_queue();                       /* flush vram_queue */
    if (!(palette_request & 0x80)) load_bg_palette(palette_request);
    hal_write(0x2000, (uint8_t)(nametable_sel | 0xB0));
    hal_write(0x2005, 0x00);
    hal_write(0x2005, scroll_y);
    hal_write(0x2001, 0x1E);
    read_joypads();
    clear_oam();
    sound_driver();                       /* sound driver */
    frame_counter++;
    if ((frame_counter & 0x3F) == 0) phase_timer++;
    A = sa; X = sx; Y = sy; fC = sc; fZ = sz; fN = sn; fV = sv;
}
void sub_D400(void) { nmi(); }

/* ------------------------------------------------------------------ */
/* power-on initialisation                                             */

/* $D502 at the next vblank, load sprite palettes and background row 0 */
void load_initial_palettes(void) {
    wait_vblank_flag();
    load_sprite_palettes();
    load_bg_palette(0);
}
void sub_D502(void) { load_initial_palettes(); }

/* $D491 init_system: clear state, palettes, both nametables, score
 * buffers. On a cold boot (no boot_signature) also reset the hi-score to
 * 20000 and the menu selection, then write the signature. */
void init_system(void) {
    digit_tile_base = 0;
    narrow_zero = 0;
    vram_queue_len = 0;
    oam_index = 0;
    paused = 0;
    palette_request = 0xFF;
    load_initial_palettes();
    oam_step = 4;
    behind_trees = 0x20;
    clear_terrain_map();                       /* clear_terrain_map */
    clear_oam();
    clear_digits(0x15);             /* clear digits buffer at $15 */
    clear_digits(0x1D);             /* ... and at $1D */
    { uint8_t bx, bc;
    if (boot_signature_valid(&bx, &bc) == 0) {                                  /* cold boot */
        clear_digits(0x3D);         /* hi-score buffer at $3D ... */
        digits[0x2A] = 2;                       /* ... = 0020000 (digit 2) */
        menu_sel = 0;
    } }
    ppu_page_offset = 0x1C;                        /* render shadow to $2000 */
    render_playfield();
    ppu_page_offset = 0x24;                        /* ... and to $2800 */
    render_playfield();
    write_boot_signature();
    sound_reset();                       /* sound_reset */
}
void sub_D491(void) { init_system(); }

/* ------------------------------------------------------------------ */
/* level loading                                                       */

/* $F000 load_level: draw stage map n into the playfield, one row of 13
 * 16x16 blocks per frame. Maps are 91 bytes at $F07A: 13 rows of 7 bytes,
 * one block type per nibble (high nibble first), the 14th nibble of each
 * row unused. n = $FF selects map 36 (the attract demo's "BATTLE CITY"
 * map); n >= 36 wraps to n - 35 (the second lap reuses maps 1-35).
 * QUIRK: n = 0 is not guarded: the pointer is advanced 255 times. */
void load_level(uint8_t n) {
    if (n == 0xFF) n = 0x24;
    else if (n >= 0x24) n = (uint8_t)(n - 0x23);
    scratch_00 = n;
    ptr_hi = 0xF0;
    ptr_lo = 0x7A;
    while (--scratch_00 != 0) ptr_advance(0x5B);
    slot_idx = 0;                              /* nibble index */
    str_ptr_lo = ptr_lo;
    str_ptr_hi = ptr_hi;
    tmp_y = 0x10;
    do {
        wait_frame();
        tmp_x = 0x10;
        do {
            uint8_t byte = ind_y(0x13, (uint8_t)(slot_idx >> 1));
            uint8_t block = (slot_idx & 1) ? (byte & 0x0F) : (byte >> 4);
            draw_block(block, tmp_x, tmp_y);   /* draw_block */
            vram_queue_len = 0;   /* rendering is off-screen work: drop the queue */
            slot_idx++;
            tmp_x = (uint8_t)(tmp_x + 0x10);
        } while (tmp_x != 0xE0);
        slot_idx++;                            /* skip the row's padding nibble */
        tmp_y = (uint8_t)(tmp_y + 0x10);
    } while (tmp_y != 0xE0);
}
void sub_F000(void) { load_level(A); }

/* ------------------------------------------------------------------ */
/* stage start                                                         */

/* $C391 set_ai_clock: enemy aggression clock for the stage,
 * $BE - 4*stage (second lap: as stage 35), minus $14 with two players. */
static void set_ai_clock(void) {
    uint8_t s = (game_mode == 1) ? 0x23 : stage_number;
    scratch_00 = (uint8_t)(s << 2);
    ai_clock = (uint8_t)(0xBE - scratch_00);
    if (menu_sel) ai_clock = (uint8_t)(ai_clock - 0x14);
}

/* $C331 start_stage: reset per-stage state, spawn the players, seed 20
 * enemies, draw the HUD, arm the eagle. */
void start_stage(void) {
    clear_bullet_slots();                       /* clear bullets */
    clear_tank_slots();                       /* clear_tank_slots */
    game_over_y = 0xF0;
    game_over_timer = 0;
    if (player_lives[0]) { spawn_tank(0); }
    if (player_lives[1]) { spawn_tank(1); }
    enemies_to_spawn = 20;
    enemies_left = 20;
    enemy_type_idx = 0;
    phase_timer = 0;
    construction_count = 0;
    shovel_timer = 0;
    tank_freeze[0] = tank_freeze[1] = 0;
    player_shield[0] = player_shield[1] = 0;
    spawn_cooldown = 0;
    powerup_x = 0;                                /* no power-up on the field */
    enemy_freeze_timer = 0;
    spawn_point_idx = 0;
    clear_kill_counts();
    draw_enemy_column();                       /* draw_enemy_column */
    wait_frame();
    draw_hud_labels();                       /* draw_hud_labels */
    draw_hud_flag();                       /* draw_hud_flag */
    load_enemy_counts();                       /* load_enemy_counts */
    base_state = 0x80;
    sound_req[18] = 1;
    stage_started = 1;
    set_ai_clock();
}
void sub_C331(void) { start_stage(); }

/* ------------------------------------------------------------------ */
/* screens                                                             */

/* $C295 redraw_screen: blank, clear the shadow, push it to the PPU, show */
void redraw_screen(void) {
    rendering_off();
    clear_terrain_map();
    render_playfield();
    rendering_on();
}
void sub_C295(void) { redraw_screen(); }

/* $C44B hiscore_screen: "HISCORE" in brick letters plus the score, shown
 * until the hi-score jingle (sound slot $15) ends, cycling background
 * palettes 5..8 every frame. */
void hiscore_screen(void) {
    rendering_off();
    ppu_page_offset = 0x1C;
    scroll_y = 0;
    nametable_sel = 0;
    clear_terrain_map();
    tmp_x = 0x10; tmp_y = 0x32;
    str_ptr_hi = 0xD2; str_ptr_lo = 0xB5;      /* "HISCORE" text at $D2B5 */
    draw_text_bricks();   /* draw_text_bricks */
    draw_hiscore_bricks();
    render_playfield();
    rendering_on();
    phase_timer = 0;
    sound_req[21] = sound_req[22] = sound_req[23] = 1;
    do {
        wait_frame();
        palette_request = (uint8_t)((frame_counter & 3) + 5);
    } while (sound_req[21]);
    palette_request = 0;
}
void sub_C44B(void) { hiscore_screen(); }

/* $C7AB title_scroll: scroll the title screen up into place (scroll_y
 * counts 1..$F0, one line per frame). Select or Start skips it. In the
 * original both exits continue at $C0A2 (the skip via pla/pla + JMP). */
void title_scroll(void) {
    scroll_y = 0;
    nametable_sel = 0;
    do {
        wait_frame();
        scroll_y++;
        if (joy_pressed[0] & 0x0C) return;     /* Select/Start: skip */
    } while (scroll_y != 0xF0);
}
void sub_C7AB(void) { title_scroll(); }

/* $C9C0 title_menu: the 1 PLAYER / 2 PLAYERS / CONSTRUCTION menu with the
 * tank cursor. Select cycles the entry. Returns the chosen entry
 * (menu_sel) on Start, or -1 after ~10.7 s idle (phase_timer == 10) when
 * construction mode has not been used (-> attract demo). Also tracks the
 * hidden two-controller cheat: with construction_count == 7 and
 * cheat_accum == $74 on Start, $C49C runs first. cheat_accum: P2 A while
 * P1 holds Down adds $10, P2 B while P1 holds Right subtracts 1. */
int title_menu(void) {
    palette_request = 3;
    clear_tank_slots();
    tank_x[0] = 0x48;
    menu_cursor_y();                       /* menu_cursor_y */
    tank_state[0] = 0x83;
    phase_timer = 0;
    tank_type[0] = 0;
    tank_tread[0] = 0;
    tank_freeze[0] = tank_freeze[1] = 0;
    scroll_y = 0;
    cheat_accum = 0;
    nametable_sel = 2;
    for (;;) {
        wait_frame();
        if ((frame_counter & 3) == 0) tank_tread[0] ^= 4;   /* tread animation */
        draw_all_tanks();                   /* draw_all_tanks */
        if (joy_pressed[0] & 0x04) { menu_sel++; phase_timer = 0; }
        if ((joy_held[0] & 0x20) && (joy_pressed[1] & 0x01)) cheat_accum = (uint8_t)(cheat_accum + 0x10);
        if ((joy_held[0] & 0x80) && (joy_pressed[1] & 0x02)) cheat_accum--;
        if (menu_sel >= 3) menu_sel = 0;
        menu_cursor_y();
        if (phase_timer == 10 && !construction_count) return -1;
        if (joy_pressed[0] & 0x08) break;
    }
    if (construction_count == 7 && cheat_accum == 0x74) credits_screen();
    palette_request = 0;
    /* the original dispatches with jmp (ptr) through tbl_menu_targets ($CA69):
     * the target address stays in ptr */
    ptr_lo = ROM8(0xCA69 + 2 * menu_sel);
    ptr_hi = ROM8(0xCA6A + 2 * menu_sel);
    return menu_sel;
}
void sub_C9C0(void) { title_menu(); }   /* not called by mechanical code */

/* $C3B5 attract_setup: the demo plays map 36 (the "BATTLE CITY" map),
 * shown as stage 30, with "BATTLE"/"CITY" brick text and 2-player lives. */
void attract_setup(void) {
    paused = 1;                                /* freezes player input */
    palette_request = 0;
    reset_game_state();
    player_lives[1] = 3;
    scroll_y = 0;
    nametable_sel = 0;
    phase_timer = 0;
    frame_counter = 0;
    clear_playfield();                       /* clear_playfield */
    stage_number = 0xFF;
    load_level(0xFF);
    stage_number = 0x1E;
    game_mode = 2;
    rendering_off();
    tmp_x = 0x1A; tmp_y = 0x46;
    str_ptr_hi = 0xD2; str_ptr_lo = 0x99;      /* "BATTLE" at $D299 */
    draw_text_bricks();
    tmp_x = 0x3C; tmp_y = 0x78;
    str_ptr_hi = 0xD2; str_ptr_lo = 0xA0;      /* "CITY" at $D2A0 */
    draw_text_bricks();
    render_playfield();
    rendering_on();
    start_stage();
    { uint8_t y; build_eagle_brick(&y); }                       /* build_eagle_brick */
    wait_frame();
    max_tank_slot = 5;                                /* highest tank slot: 4 enemies */
}
void sub_C3B5(void) { attract_setup(); }

/* $C41D attract_run: run the demo until the stage ends (returns 1) or
 * Select/Start is pressed (returns 0; the original aborts with pla/pla,
 * clears the screen and jumps back to the menu). */
int attract_run(void) {
    for (;;) {
        wait_frame();
        if (joy_pressed[0] & 0x0C) {
            vram_queue_len = 0;
            clear_screen();               /* clear_screen */
            return 0;
        }
        attract_input();                   /* demo input */
        main_loop();                   /* main_loop */
        draw_powerup();
        draw_all_tanks();
        draw_bullets();
        if (stage_over()) break;                /* stage_over: nonzero = over */
    }
    vram_queue_len = 0;
    return 1;
}
void sub_C41D(void) { attract_run(); }   /* not called by mechanical code */

/* ------------------------------------------------------------------ */
/* the game                                                            */

enum { MENU_TIMEOUT = -1 };

/* $C070 reset and the whole game as a state machine. */
void game_main(void) {
    /* $C070: interrupts off, NMI off, wait two vblanks (PPU warm-up) */
    fI = 1;
    hal_write(0x2000, 0x10);
    fD = 0;
    for (int i = 0; i < 2; i++) {
        wait_vblank_flag();
        hal_write(0x2001, 0x06);
    }
    S = 0x7F;   /* ldx #$7F / txs: the stack occupies $0100-$017F, below the VRAM queue */
    init_system();
    scroll_y = 0;
    nametable_sel = 0;
    rendering_on();

title_screen:                                  /* $C095 */
    draw_title_logo();                       /* draw_title_logo */
    construction_count = 0;
title_loop:                                    /* $C09C */
    clear_screen();                       /* clear_screen */
    title_scroll();
menu:                                          /* $C0A2 */
    switch (title_menu()) {
    case MENU_TIMEOUT:
        attract_setup();                       /* $C0A5 */
        if (attract_run()) goto title_loop;    /* $C0A8, $C0AB */
        goto menu;                             /* aborted by Select/Start */
    case 0:                                    /* $CA6F 1 PLAYER */
        max_tank_slot = 5;
        new_game();                   /* new_game */
        goto stage_intro;
    case 1:                                    /* $CA74 2 PLAYERS */
        max_tank_slot = 7;
        new_game();
        goto stage_intro;
    default:                                   /* $CA7E CONSTRUCTION */
        max_tank_slot = 7;
        goto construction;
    }

construction:                                  /* $C0AE */
    if (!construction_count) {
        rendering_off();
        clear_playfield();                   /* clear_playfield */
        render_playfield();
        rendering_on();
    }
    clear_tank_slots();
    tank_x[0] = 0x10;                          /* slot 0 is the cursor */
    tank_y[0] = 0x18;
    tank_state[0] = 0x84;
    tank_type[0] = 0;
    behind_trees = 0;
    tank_tread[0] = 0;
    edit_cycle_armed = 0;
    edit_hold_count = 0;
    edit_tile_sel = 0;
    scroll_y = 0;
    nametable_sel = 0;
    tank_freeze[0] = tank_freeze[1] = 0;
    if (!construction_count) { uint8_t y; build_eagle_brick(&y); }
    for (;;) {                                 /* $C0EA */
        wait_frame();
        construction_cursor_input();
        clamp_cursor();
        if (frame_counter & 0x10) draw_all_tanks();   /* blinking cursor */
        int place = 0;
        if (!(joy_held[0] & 0xF0)) {           /* not moving */
            if (joy_pressed[0] & 0x01) {       /* A: next tile */
                if (!edit_cycle_armed) edit_cycle_armed++;
                else if (++edit_tile_sel == 0x0E) edit_tile_sel = 0;
                place = 1;
            } else if (joy_pressed[0] & 0x02) {   /* B: previous tile */
                if (!edit_cycle_armed) edit_cycle_armed++;
                else if (--edit_tile_sel == 0xFF) edit_tile_sel = 0x0D;
                place = 1;
            }
        }
        if (!place && (joy_held[0] & 0x03)) place = 1;   /* $C13E: A/B held */
        if (place) draw_cursor_block();        /* $C144 place the tile */
        if (joy_pressed[0] & 0x08) break;      /* Start: leave */
    }
    behind_trees = 0x20;                       /* $C150 */
    construction_count++;
    goto menu;

stage_intro:                                   /* $C159 */
    wait_frame();
    sound_reset();                       /* sound_reset */
    ppu_page_offset = 0x1C;
    scroll_y = 0;
    nametable_sel = 0;
    paused = 0;
    palette_request = 4;
    curtain_close();                       /* "STAGE nn" screen */
    for (;;) {                                 /* $C172 */
        stage_intro_text();
        if (stage_started) break;                     /* not the first stage: no select */
        if (joy_pressed[0] & 0x08) break;      /* Start */
        /* stage select: A up, B down; held repeats every 8 frames */
        if ((joy_pressed[0] & 0x01) || ((joy_held[0] & 0x01) && !(frame_counter & 7))) {
            frame_counter = 0;
            if (++stage_number == 0x24) stage_number = 0x23;
        } else if ((joy_pressed[0] & 0x02) || ((joy_held[0] & 0x02) && !(frame_counter & 7))) {
            frame_counter = 0;
            if (--stage_number == 0) stage_number = 1;
        }
    }

    /* $C1C5 play_stage */
    sound_req[1] = sound_req[2] = sound_req[3] = 1;
    if (!construction_count) {
        clear_playfield();
        load_level(stage_number);
        { uint8_t y; build_eagle_brick(&y); }
    } else {
        load_custom_level();                   /* load_custom_level */
    }
    vram_queue_len = 0;
    upload_attributes();
    curtain_open();
    palette_request = 0;
    wait_frame();
    start_stage();
    do {                                       /* $C1F9 the stage */
        wait_frame();
        if (!paused) main_loop();      /* main_loop */
        draw_powerup();                   /* draw_powerup */
        draw_bullets();                   /* draw_bullets */
        draw_all_tanks();                   /* draw_all_tanks */
        if (joy_pressed[0] & 0x08) {           /* Start: pause toggle */
            paused ^= 1;
            sound_req[0] = paused;
        }
        draw_pause_banner();                   /* draw_pause_banner */
        } while (!stage_over());                    /* stage_over */

    /* $C223 stage end: keep the world running ~2 s (until phase_timer == 2;
     * with a game-over banner up, from $FE: 4 units ~4.3 s) */
    phase_timer = 0;
    frame_counter = 0;
    sound_req[17] = 0;
    sound_req[18] = 0;
    if (game_over_timer) phase_timer = 0xFE;
    do {
        wait_frame();
        clear_pads_if_base_lost();                   /* clear_pads_if_base_lost */
        main_loop();
        draw_powerup();
        draw_all_tanks();
        draw_bullets();
        palette_cycle();                   /* palette_cycle */
    } while (phase_timer != 2);
    sound_reset();
    stage_tally();                       /* stage_tally */
    stage_number++;
    if (stage_number == 0x47) { stage_number = 1; game_mode = 0; }   /* after 70: lap 1 */
    if (stage_number == 0x24) game_mode = 1;                         /* 36..70: lap 2 */
    if ((uint8_t)(player_lives[0] + player_lives[1]) != 0 && base_state == 0x80)
        goto stage_intro;

    /* $C283 game over */
    game_over_screen();                       /* game-over screen */
    { uint8_t hx;
    if (update_hiscore(&hx)) {   /* nonzero = new record */
        hiscore_screen();
        redraw_screen();
    } }
    goto title_screen;
}

void bc_reset_lifted(void) { game_main(); }
void sub_C070(void) { game_main(); }
void sub_CA6F(void) { max_tank_slot = 5; new_game(); A = 0; SZN(A); }   /* entry fragments: only */
void sub_CA74(void) { max_tank_slot = 7; new_game(); A = 0; SZN(A); }   /* reached via title_menu, */
void sub_CA7E(void) { max_tank_slot = 7; }                        /* which is lifted above */
