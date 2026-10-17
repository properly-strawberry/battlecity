/* flow_hud: per-frame game-flow helpers, HUD side panel, construction-mode
 * helpers, menu cursor, stage-over check ($C2A2-$CC5C, scattered).
 *
 * Panel layout (playfield is cols 2..27; the side panel is cols 29..30):
 *  rows 3..12  enemy icons, 2 per row, 20 in all
 *  row 17/18   "IP" label / P1 lives;  row 20/21  "IIP" label / P2 lives
 *  rows 23..24 flag, row 25 stage number */
#include "common.h"

#include "lifted.h"

static inline void vq_put(uint8_t *x, uint8_t v) { RAM[0x180 + *x] = v; (*x)++; }
static inline void set_ptr(uint16_t p) { ptr_lo = (uint8_t)p; ptr_hi = (uint8_t)(p >> 8); }

/* ------------------------------------------------------------------ */
/* $C2A2 clear_pads_if_base_lost
 * Once the eagle is no longer intact ($80), both pads are forced to 0
 * every frame (no more player input after the base is destroyed). */
void clear_pads_if_base_lost(void) {
    if (base_state != 0x80) {
        joy_held[0] = joy_held[1] = 0;
        joy_pressed[0] = joy_pressed[1] = 0;
    }
}
BC_WEAK void sub_C2A2(void) {
    clear_pads_if_base_lost();
    A = (base_state != 0x80) ? 0 : base_state;
    fC = (base_state >= 0x80);
    if (base_state == 0x80) { fZ = 1; fN = 0; } else SZN(A);
}

/* $C2BD reset_game_state: zero levels / bonus-life latches / intro timer,
 * 3 lives each (P2 gets 0 in a one-player game), stage 1, lap 0. */
void reset_game_state(void) {
    player_level[0] = 0;
    player_level[1] = 0;
    extra_life_given[0] = 0;
    extra_life_given[1] = 0;
    stage_started = 0;
    player_lives[0] = 3;
    player_lives[1] = 3;
    spawn_point_idx = 3;
    if (menu_sel == 0) player_lives[1] = 0;
    stage_number = 1;
    game_mode = 0;
}
BC_WEAK void sub_C2BD(void) { reset_game_state(); A = 0; SZN(A); }

/* $C2B3 new_game: clear both score digit buffers ($15+... via X=$15 and
 * X=$1D; clear_digits wipes 8 bytes up to X, i.e. score of P1 / P2),
 * then falls into reset_game_state. */
void new_game(void) {
    clear_digits(0x15);   /* $D9FE */
    clear_digits(0x1D);
    reset_game_state();
}
BC_WEAK void sub_C2B3(void) { new_game(); A = 0; SZN(A); }

/* $C31D palette_cycle: every 64 frames request palette 2, and at the
 * half-way point (frame & $3F == $20) palette 1: the flashing palette. */
void palette_cycle(void) {
    uint8_t f = frame_counter & 0x3F;
    if (f == 0) palette_request = 2;
    else if (f == 0x20) palette_request = 1;
}
BC_WEAK void sub_C31D(void) {
    uint8_t f = frame_counter & 0x3F;
    palette_cycle();
    if (f == 0) A = 2; else if (f == 0x20) A = 1; else A = f;
    SZN(A);
}

/* ------------------------------------------------------------------ */
/* HUD */

/* $C894 hud_enemy_cell: enemy icon n -> panel cell col 29+(n&1), row 3+(n>>1) */
void hud_enemy_cell(uint8_t n, uint8_t *col, uint8_t *row) {
    *col = (uint8_t)(0x1D + (n & 1));
    *row = (uint8_t)(3 + (n >> 1));
}
BC_WEAK void sub_C894(void) { uint8_t c, r; hud_enemy_cell(A, &c, &r); X = c; Y = r; }

/* $C8A2 draw_enemy_icon: draw icon n (tile run at $D362) */
void draw_enemy_icon(uint8_t n) {
    uint8_t c, r;
    hud_enemy_cell(n, &c, &r);
    set_ptr(0xD362);
    draw_tiles(c, r);
}
BC_WEAK void sub_C8A2(void) { draw_enemy_icon(A); }

/* $C8B1 erase_enemy_icon: erase icon n (blank run at $D36B) */
void erase_enemy_icon(uint8_t n) {
    uint8_t c, r;
    hud_enemy_cell(n, &c, &r);
    set_ptr(0xD36B);
    draw_tiles(c, r);
}
BC_WEAK void sub_C8B1(void) { erase_enemy_icon(A); }

/* $C8C0 draw_enemy_column: icons 18,16,...,0 (each call draws a row of
 * two icons: n and n+1 share row 3+(n>>1)... the tile run is 2 wide).
 * Leaves slot_idx = $FE. */
void draw_enemy_column(void) {
    slot_idx = 0x12;
    do {
        draw_enemy_icon(slot_idx);
        slot_idx = (uint8_t)(slot_idx - 2);
    } while (!(slot_idx & 0x80));
}
BC_WEAK void sub_C8C0(void) { draw_enemy_column(); }

/* $C830 draw_hud_labels: "IP" always; "IIP" in 2-player or demo mode */
void draw_hud_labels(void) {
    set_ptr(0xD2AB);
    draw_tiles(0x1D, 0x11);
    if (game_mode == 2 || menu_sel != 0) {
        set_ptr(0xD2AE);
        draw_tiles(0x1D, 0x14);
    }
}
BC_WEAK void sub_C830(void) { draw_hud_labels(); }

/* $C7C8 draw_hud: lives panel. Per player: label tiles, then lives-1
 * (floored at 0) as a number. The second player is drawn first (slot 1),
 * slot 0 only when a 2nd player exists or in demo (game_mode 2). */
void draw_hud(void) {
    slot_idx = 1;
    narrow_zero = 1;
    digit_tile_base = 0x6E;
    set_ptr(0xD341);
    draw_tiles(0x1D, 0x12);
    if (game_mode == 2 || menu_sel != 0) {
        set_ptr(0xD341);
        draw_tiles(0x1D, 0x15);
    } else {
        slot_idx = 0;
    }
    do {
        uint8_t lives = (uint8_t)(player_lives[slot_idx] - 1);
        if (lives & 0x80) lives = 0;          /* lives 0 -> shown as 0 */
        number_to_digits(lives);              /* $DA13 */
        uint8_t col = align_number(0x19, 0x36);    /* $D934 */
        scratch_00 = slot_idx;
        uint8_t row = (uint8_t)(slot_idx * 3 + 0x12);
        draw_hud_number(col, row);
        slot_idx--;
    } while (!(slot_idx & 0x80));
    digit_tile_base = 0;
    narrow_zero = 0;
}
BC_WEAK void sub_C7C8(void) { draw_hud(); }

/* ------------------------------------------------------------------ */
/* $C728 stage_over: 1 (Z clear) = stage finished, 0 = keep playing.
 *  - eagle destroyed (base_state 0): arm the centred banner, finished
 *  - all enemies dead: finished (banner not armed here)
 *  - both players out of lives (sum 0): finished, banner armed (game over)
 *  - otherwise: keep playing */
uint8_t stage_over(void) {
    if (base_state != 0) {
        if (enemies_left == 0) return 1;
        if ((uint8_t)(player_lives[0] + player_lives[1]) != 0) return 0;
    }
    game_over_x = 0x70;
    game_over_y = 0xF0;
    game_over_dir = 0;
    game_over_timer = 0x11;   /* centred */
    frame_counter = 0;
    return 1;
}
BC_WEAK void sub_C728(void) { A = stage_over(); SZN(A); fC = 0; }

/* $C71E clear_kill_counts: zero p1_kills[4] and p2_kills[4] */
void clear_kill_counts(void) { for (int i = 0; i < 8; i++) p1_kills[i] = 0; }
BC_WEAK void sub_C71E(void) { clear_kill_counts(); A = 0; X = 0xFF; SZN(X); }

/* ------------------------------------------------------------------ */
/* attract demo */

/* $C642 attract_input: synthesises the pad bytes of both "players" in the
 * demo. Per slot: aim at the powerup if one is on the field and not being
 * collected; else at the first active, non-flashing enemy found in tank
 * slot order +2, +4, +3 (offset from the slot's index); else no buttons.
 * The direction from heading_to_target is mapped through
 * {$13,$43,$23,$83} (direction | fire/move bit pattern). A tank below
 * y>=$C8 gets its newly-pressed d-pad bits masked off. */
void attract_input(void) {
    static const uint8_t pad_for_dir[4] = {0x13, 0x43, 0x23, 0x83};
    static const uint8_t order[3] = {2, 4, 3};
    slot_idx = 1;
    do {
        uint8_t x = slot_idx;
        uint8_t a = 0, a_dir = 0;
        int found = 0;
        if (powerup_x != 0 && powerup_flash == 0) {
            seek_x = powerup_x;
            seek_y = powerup_y;
            a_dir = heading_to_target(x);   /* $DDA2 */
            found = 1;
        } else {
            for (int i = 0; i < 3 && !found; i++) {
                uint8_t o = order[i];
                uint8_t st = tank_state[(uint8_t)(o + x)];
                if ((st & 0x80) && st < 0xE0) {
                    seek_x = tank_x[(uint8_t)(o + x)];
                    seek_y = tank_y[(uint8_t)(o + x)];
                    a_dir = heading_to_target(x);   /* $DDA2 */
                    found = 1;
                }
            }
        }
        if (found) a = pad_for_dir[a_dir & 3];
        x = slot_idx;
        joy_held[x] = a;
        joy_pressed[x] = a;
        if (tank_y[x] >= 0xC8) joy_pressed[x] &= 0xF0;
        slot_idx--;
    } while (!(slot_idx & 0x80));
}
BC_WEAK void sub_C642(void) { attract_input(); }

/* ------------------------------------------------------------------ */
/* construction mode */

/* $C6C6 draw_cursor_block: draw tile (edit_tile_sel & 15) at the cursor
 * (tank_x, tank_y) with draw_block */
void draw_cursor_block(void) {
    draw_block(edit_tile_sel & 0x0F, tank_x[0], tank_y[0]);   /* $D80B */
}
BC_WEAK void sub_C6C6(void) { draw_cursor_block(); }

/* $C6D2 construction_cursor_input: moves the cursor (tank slot 0) by one
 * 16-pixel step. A held direction counts up edit_hold_count; a fresh press moves
 * at once, then after 20 frames of holding it auto-repeats every 15.
 * Pressing a direction also clears edit_cycle_armed. */
void construction_cursor_input(void) {
    uint8_t dir;
    if (joy_held[0] & 0xF0) { edit_hold_count++; edit_cycle_armed = 0; }
    else edit_hold_count = 0;
    if (edit_hold_count == 0x14) {
        edit_hold_count = 0x0F;
        dir = decode_direction(joy_held[0]);   /* $E451 */
    } else {
        if (!(joy_pressed[0] & 0xF0)) return;
        dir = decode_direction(joy_pressed[0]);
        if (dir & 0x80) return;                /* no direction bit set */
    }
    tank_x[0] = (uint8_t)(tank_x[0] + (uint8_t)(bus_read((uint16_t)(0xD3D5 + dir)) << 4));
    tank_y[0] = (uint8_t)(tank_y[0] + (uint8_t)(bus_read((uint16_t)(0xD3D9 + dir)) << 4));
}
BC_WEAK void sub_C6D2(void) { construction_cursor_input(); }

/* $C8D0 clamp_cursor: keep the construction cursor within $18..$D8 */
void clamp_cursor(void) {
    if (tank_x[0] >= 0xD8) tank_x[0] = 0xD8;
    if (tank_x[0] < 0x18) tank_x[0] = 0x18;
    if (tank_y[0] >= 0xD8) tank_y[0] = 0xD8;
    if (tank_y[0] < 0x18) tank_y[0] = 0x18;
}
BC_WEAK void sub_C8D0(void) { clamp_cursor(); A = tank_y[0]; fC = (A >= 0x18); SZN(A); }

/* $C9B0 clear_playfield: origin (2,2), 26x26 cells, then reset_terrain_map
 * (fill $11, clear the 26x26 playfield to 0) */
void clear_playfield(void) {
    tmp_x = 2; tmp_y = 2;
    slot_idx = 0x1A; cell_w = 0x1A;
    reset_terrain_map();   /* $D7CC */
}
BC_WEAK void sub_C9B0(void) { clear_playfield(); }

/* $CA85 menu_cursor_y: cursor tank y = menu_sel*16 + $8B */
void menu_cursor_y(void) { tank_y[0] = (uint8_t)((menu_sel << 4) + 0x8B); }
BC_WEAK void sub_CA85(void) { menu_cursor_y(); A = tank_y[0]; SZN(A); }

/* $CB5D load_custom_level: draw the eagle's bottom bricks of a player-built
 * level (tile runs $D3A5 / $D3A8 at (14,26),(14,27)), then clear the top
 * two attribute bits of terrain_map[$3F3] and queue it to $23F3. */
void load_custom_level(void) {
    set_ptr(0xD3A5); draw_tiles(0x0E, 0x1A);
    set_ptr(0xD3A8); draw_tiles(0x0E, 0x1B);
    uint8_t q = vram_queue_len;
    vq_put(&q, 0x23);
    vq_put(&q, 0xF3);
    uint8_t v = terrain_map[0x3F3] & 0x3F;
    terrain_map[0x3F3] = v;
    vq_put(&q, v);
    vq_put(&q, 0xFF);
    vram_queue_len = q;
}
BC_WEAK void sub_CB5D(void) { load_custom_level(); }

/* $CC5C queue_map_row: queue map row Y (32 cells, col 0) as one VRAM
 * record. If RAM[$63] is nonzero that byte fills the row instead (used by
 * the stage-intro curtain: $11 = grey fill; 0 = reveal real map). Leaves
 * ptr at the row. */
void queue_map_row(uint8_t row) {
    uint8_t lo;
    uint8_t hi = cell_to_addr(0, row, &lo);
    ptr_hi = hi; ptr_lo = lo;
    uint8_t q = vram_queue_len;
    vq_put(&q, (uint8_t)(hi + 0x1C));
    vq_put(&q, lo);
    for (uint8_t y = 0; y < 0x20; y++) {
        uint8_t v = row_fill_tile;
        if (v == 0) v = ind_y(0x11, y);
        vq_put(&q, v);
    }
    vq_put(&q, 0xFF);
    vram_queue_len = q;
}
BC_WEAK void sub_CC5C(void) { queue_map_row(Y); X = vram_queue_len; Y = 0x20; }

/* ------------------------------------------------------------------ */
/* $C2E6 main_loop: one gameplay frame. Order matters. */
void main_loop(void) {
    update_tank_terrain();   /* update_tank_terrain: cache terrain pointer per tank */
    player_control();   /* player_control */
    move_all_tanks();   /* move_all_tanks */
    clear_tank_occupancy();   /* clear_tank_occupancy */
    update_bullets();   /* update_bullets */
    shovel_and_base();   /* shovel_and_base */
    draw_shield();   /* draw_shield */
    player_fire();   /* player_fire */
    enemy_fire();   /* enemy_fire */
    spawn_enemy();   /* spawn_enemy */
    bullet_vs_terrain();   /* bullet_vs_terrain */
    bullet_vs_bullet();   /* bullet_vs_bullet */
    bullet_vs_tank();   /* bullet_vs_tank */
    {   /* $E972 powerup_pickup. QUIRK: on a pickup the original does
         * pla/pla and tail-jumps to the effect, whose RTS returns to OUR
         * caller: the rest of this frame's calls ($C310-$C319) are skipped.
         * powerup_pickup returns 1 in that case. */
        uint8_t xo;
        if (powerup_pickup(&xo)) return;
    }
    game_over_slide();   /* game_over_slide */
    update_engine_sound();   /* update_engine_sound */
    draw_hud();            /* $C7C8 */
    palette_cycle();       /* $C31D */
}
BC_WEAK void sub_C2E6(void) { main_loop(); }
