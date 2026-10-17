/* Bullets, firing, bullet collisions, power-ups, eagle/shovel walls.
 * ($E02E-$EA48 region plus the eagle wall builders $CAF5/$CB9E/$CC08)
 *
 * Model (verified by these routines):
 *  - 10 bullet slots: 0,1 = player 1/2 primary bullet, 2..7 = bullets of
 *    enemy tanks 2..7, 8,9 = player 1/2 SECOND bullet (level-3 stars). A
 *    bullet slot index is also the owner tank slot for slots 0..7; slot
 *    8/9 belong to player (slot&1).
 *  - bullet_state: high nibble = mode ($00 free, $10-$30 explosion
 *    animation, $40 flying), low 2 bits = heading (0 up, 1 left, 2 down,
 *    3 right; tbl_dx/tbl_dy at $E46C/$E470). During an explosion the low
 *    nibble counts down 3,2,1,0 then the high nibble drops by one.
 *  - bullet_flags: bit0 fast (steps twice per frame), bit1 powerful
 *    (destroys concrete). Set by fire_bullet from the tank type.
 *  - Terrain tiles: $00-$0F brick (low nibble = the four 4x4 quarters),
 *    $10 concrete (destroyed only by powerful bullets), $11 playfield
 *    border (never destroyed); tiles >= $12 (water, trees, ice, bit 7 =
 *    "tank standing here" mark, ...) do not stop bullets.
 *  - Eagle: base_state $80 intact, $27 -> 0 explosion countdown. */
#include "common.h"
#include "lifted.h"

/* ---- routines owned by cells.c (merged) ---- */
uint8_t draw_tiles(uint8_t col, uint8_t row);
void quarter_mask(void);
uint8_t test_quarter(uint8_t y);
void write_cell(uint8_t v, uint8_t x, uint8_t y);
void clear_quarter(uint8_t x, uint8_t y);
void coords_to_map(uint8_t px, uint8_t py);

/* ---- routines owned by other packages ---- */
void sub_DBF0(void);   /* bare RTS */
uint8_t draw_destroyed_eagle(void);

/* zero-page,X indexing wraps inside page 0 */
#define ZPX(base, x) RAM[(uint8_t)((base) + (x))]
#define B_X     0xB8
#define B_Y     0xC2
#define B_STATE 0xCC
#define B_FLAGS 0xD6
#define T_X     0x90
#define T_Y     0x98
#define T_STATE 0xA0
#define T_TYPE  0xA8

/* ROM data tables: names and addresses in src/tables.h */
#define ROM16(a) ((uint16_t)(ROM8(a) | (ROM8((a) + 1) << 8)))

static inline uint8_t abs8(uint8_t d) { return (d & 0x80) ? (uint8_t)(-d) : d; }
static inline void vq_put(uint8_t *x, uint8_t v) { RAM[0x180 + *x] = v; (*x)++; }
/* a tank slot is "alive" when bit 7 is set and the state is below $E0
 * ($E0+ = spawning/exploding) */
static inline int tank_alive(uint8_t st) { return (st & 0x80) && st < 0xE0; }



/* ======================================================================
 * Bullet movement / animation
 * ====================================================================== */

/* $E063 bullet_step: move bullet x two pixels along heading y. */
void bullet_step(uint8_t x, uint8_t y) {
    ZPX(B_X, x) = (uint8_t)(ZPX(B_X, x) + (uint8_t)(ROM8(TBL_DX + y) << 1));
    ZPX(B_Y, x) = (uint8_t)(ZPX(B_Y, x) + (uint8_t)(ROM8(TBL_DY + y) << 1));
}
void sub_E063(void) { bullet_step(X, Y); A = ZPX(B_Y, X); SZN(A); }

/* $E051 bullet_move: a flying bullet steps 2 px, or 4 px if fast. */
void bullet_move(uint8_t x) {
    uint8_t dir = ZPX(B_STATE, x) & 3;
    bullet_step(x, dir);
    if (ZPX(B_FLAGS, x) & 1) bullet_step(x, dir);
}
void sub_E051(void) { bullet_move(X); A = 0; SZN(A); }

/* $E076 bullet_explode_tick: advance the hit animation $33,$32,$31,$30,
 * $23,... down to $00 (slot free). */
void bullet_explode_tick(uint8_t x) {
    ZPX(B_STATE, x)--;
    uint8_t s = ZPX(B_STATE, x);
    if ((s & 0x0F) == 0) {
        uint8_t hi = (uint8_t)((s & 0xF0) - 0x10);
        if (hi != 0) hi |= 0x03;
        ZPX(B_STATE, x) = hi;
    }
}
void sub_E076(void) { bullet_explode_tick(X); A = ZPX(B_STATE, X); SZN(A); }

/* $E03C bullet_dispatch: per-slot handler chosen by the state's high
 * nibble through tbl_bullet_move ($E4D8): free -> RTS, 1x/2x/3x ->
 * explosion tick, 4x -> move. ptr ($11) receives the handler address. */
void bullet_dispatch(uint8_t x) {
    uint8_t idx = (uint8_t)((ZPX(B_STATE, x) >> 3) & 0xFE);
    uint16_t h = ROM16(TBL_BULLET_MOVE + idx);
    ptr_lo = (uint8_t)h;
    ptr_hi = (uint8_t)(h >> 8);
    switch (h) {
    case 0xDBF0: break;                       /* nothing to do */
    case 0xE076: bullet_explode_tick(x); break;
    case 0xE051: bullet_move(x); break;
    default: break;   /* QUIRK: a state high nibble >= 5 would read past the
                       * 5-entry table and jump into the middle of other code.
                       * Never happens: every writer keeps bullet_state < $50
                       * (difftest in_domain()), and x is 0..9 (update_bullets). */
    }
}
void sub_E03C(void) { bullet_dispatch(X); }

/* $E02E update_bullets: run all 10 bullet slots, 9 down to 0. */
void update_bullets(void) {
    slot_idx = 9;
    do {
        bullet_dispatch(slot_idx);
        slot_idx--;
    } while (!(slot_idx & 0x80));
}
void sub_E02E(void) { update_bullets(); X = 0; A = 0; SZN(A); }

/* ======================================================================
 * Firing
 * ====================================================================== */

/* $E08C fire_bullet: tank x launches a bullet if its slot is free. The
 * bullet starts 8 px in front of the tank centre. Tank type decides the
 * flags: $00 and $80,$90,$A0,$B0,$D0..$F0 -> normal; $10-$50,$70 and $C0
 * -> fast; $60 -> fast + powerful. (Player tank_type = player_level, so a
 * 1-star player already has fast bullets, 3 stars two bullets, 4 stars
 * powerful bullets.) Player shots ask for sound 15. */
void fire_bullet(uint8_t x) {
    if (ZPX(B_STATE, x) != 0) return;
    if (x < 2) sound_req[15] = 1;
    uint8_t dir = ZPX(T_STATE, x) & 3;
    ZPX(B_STATE, x) = dir | 0x40;
    ZPX(B_X, x) = (uint8_t)((uint8_t)(ROM8(TBL_DX + dir) << 3) + ZPX(T_X, x));
    ZPX(B_Y, x) = (uint8_t)((uint8_t)(ROM8(TBL_DY + dir) << 3) + ZPX(T_Y, x));
    ZPX(B_FLAGS, x) = 0;
    uint8_t t = ZPX(T_TYPE, x) & 0xF0;
    if (t == 0) return;
    if (t == 0xC0) { ZPX(B_FLAGS, x) = 1; return; }
    if (t == 0x60) { ZPX(B_FLAGS, x) = 3; return; }
    if (t & 0x80) return;
    ZPX(B_FLAGS, x) = 1;
}
void sub_E08C(void) { fire_bullet(X); A = ZPX(B_FLAGS, X); SZN(A); }

/* $E122 player_fire: for each player alive and just pressing A or B, fire.
 * A tank_type of $40..$7F (3+ stars) may keep a second bullet: if the
 * first is still flying and slot x+8 is free, the old bullet is moved to
 * slot x+8 and a new one is fired from slot x. */
void player_fire(void) {
    slot_idx = 1;
    do {
        uint8_t x = slot_idx;
        uint8_t st = RAM[T_STATE + x];
        if (tank_alive(st) && (joy_pressed[x] & 3)) {
            int fire = 1;
            if ((RAM[T_TYPE + x] & 0xC0) == 0x40 && RAM[B_STATE + x] != 0) {
                if (RAM[B_STATE + 8 + x] != 0) {
                    fire = 0;                       /* both bullets busy */
                } else {
                    RAM[B_STATE + 8 + x] = RAM[B_STATE + x];
                    RAM[B_X + 8 + x] = RAM[B_X + x];
                    RAM[B_Y + 8 + x] = RAM[B_Y + x];
                    RAM[B_FLAGS + 8 + x] = RAM[B_FLAGS + x];
                    RAM[B_STATE + x] = 0;
                }
            }
            if (fire) fire_bullet(x);
        }
        slot_idx--;
    } while (!(slot_idx & 0x80));
}
void sub_E122(void) { player_fire(); X = 0xFF; A = 0xFF; SZN(A); }

/* $E162 enemy_fire: unless the TIMER power-up is active, each live enemy
 * (slots 7..2) fires with probability 1/32 per frame. */
void enemy_fire(void) {
    if (enemy_freeze_timer != 0) return;
    for (uint8_t x = 7; x != 1; x--) {
        if (tank_alive(RAM[T_STATE + x])) {
            if ((rng() & 0x1F) == 0) fire_bullet(x);
        }
    }
}
void sub_E162(void) { enemy_fire(); X = 1; A = 1; SZN(A); }

/* ======================================================================
 * Bullet vs terrain
 * ====================================================================== */

/* $E69A hit_terrain: evaluate the quarter selected by cell_x/cell_y of the
 * cell at ptr+y, for bullet slot_idx. Returns nonzero only when a brick
 * quarter was cleared (the caller then tests the neighbouring quarter).
 * *yout is Y as the original leaves it (only the eagle path changes it).
 *  - empty quarter: nothing happens
 *  - eagle tiles ($C8-$CB) while the eagle lives: base_state = $27, sounds
 *    11 and 7, eagle redrawn destroyed; the bullet explodes
 *  - tiles >= $12 never stop a bullet
 *  - otherwise the bullet becomes an explosion ($33); tile $11 (border):
 *    nothing more; powerful bullet: whole cell cleared (brick or concrete);
 *    concrete $10 vs normal bullet: nothing; brick: that quarter cleared.
 *  - sound 13 on a bounce, 12 on destroyed terrain; only for player bullets */
uint8_t hit_terrain(uint8_t y, uint8_t *yout) {
    *yout = y;
    quarter_mask();
    if (test_quarter(y) == 0) return 0;
    uint8_t c = ind_y(0x11, y);
    if ((c & 0xFC) == 0xC8 && base_state != 0) {
        base_state = 0x27;
        sound_req[11] = 1;
        sound_req[7] = 1;
        *yout = draw_destroyed_eagle();
        ZPX(B_STATE, slot_idx) = 0x33;
        return 0;
    }
    c = ind_y(0x11, y);
    if (c >= 0x12) return 0;
    uint8_t x = slot_idx;
    ZPX(B_STATE, x) = 0x33;
    if (ind_y(0x11, y) != 0x11) {
        if (ZPX(B_FLAGS, x) & 2) {                 /* powerful: wipe the cell */
            write_cell(0, x, y);
            sound_req[12] = 1;
            return 0;
        }
        if (ind_y(0x11, y) != 0x10) {              /* brick */
            if (x < 2) sound_req[12] = 1;
            clear_quarter(x, y);
            return 1;
        }
    }
    if (x < 2) sound_req[13] = 1;                  /* bounced off concrete/border */
    return 0;
}
void sub_E69A(void) {
    uint8_t yo;
    A = hit_terrain(Y, &yo); Y = yo; SZN(A);
}

/* $E693 probe_terrain: test the pixel (px,py); returns hit_terrain's
 * result. *yout is Y on return. */
uint8_t probe_terrain(uint8_t px, uint8_t py, uint8_t *yout) {
    cell_x = px;
    cell_y = py;
    coords_to_map(px, py);
    return hit_terrain(0, yout);
}
void sub_E693(void) {
    uint8_t yo;
    A = probe_terrain(X, Y, &yo); Y = yo; SZN(A);
}

/* $E604 bullet_vs_terrain: for each flying bullet, probe the terrain under
 * its nose twice (centre line and one pixel to the side). A slow bullet
 * (flags 0) is only tested every other frame; fast ones every frame. A
 * successful brick hit then evaluates the neighbouring quarter (4 px
 * across) of the same cell. */
void bullet_vs_terrain(void) {
    slot_idx = 9;
    do {
        uint8_t x = slot_idx;
        if ((ZPX(B_STATE, x) & 0xF0) == 0x40 &&
            (ZPX(B_FLAGS, x) != 0 || ((x ^ frame_counter) & 1))) {
            uint8_t dir = ZPX(B_STATE, x) & 3;
            uint8_t adx = abs8(ROM8(TBL_DX2 + dir));
            scratch_54 = adx;
            scratch_64 = (uint8_t)(adx << 2);
            uint8_t ady = abs8(ROM8(TBL_DY2 + dir));
            scratch_55 = ady;
            scratch_65 = (uint8_t)(ady << 2);
            uint8_t yo;
            if (probe_terrain(ZPX(B_X, x), ZPX(B_Y, x), &yo)) {
                x = slot_idx;
                cell_y = (uint8_t)(ZPX(B_Y, x) + scratch_64);
                cell_x = (uint8_t)(ZPX(B_X, x) + scratch_65);
                hit_terrain(0, &yo);
            }
            x = slot_idx;
            if (probe_terrain((uint8_t)(ZPX(B_X, x) - scratch_55),
                              (uint8_t)(ZPX(B_Y, x) - scratch_54), &yo)) {
                x = slot_idx;
                cell_y = (uint8_t)(ZPX(B_Y, x) - scratch_64 - scratch_54);
                cell_x = (uint8_t)(ZPX(B_X, x) - scratch_65 - scratch_55);
                hit_terrain(0, &yo);
            }
        }
        slot_idx--;
    } while (!(slot_idx & 0x80));
}
void sub_E604(void) { bullet_vs_terrain(); X = 0xFF; A = 0; SZN(A); fC = 0; }

/* ======================================================================
 * Power-up spawn helpers
 * ====================================================================== */

/* $E902 powerup_coord: 0..3 -> $30,$60,$90,$C0 (also leaves n in scratch_00) */
uint8_t powerup_coord(uint8_t n) {
    scratch_00 = n;
    return (uint8_t)((uint8_t)((uint8_t)((uint8_t)(n << 1) + n) << 1) + 6) << 3;
}
void sub_E902(void) { A = powerup_coord(A); SZN(A); fC = 0; }

/* ---- power-up effects (tail-jumped from powerup_pickup) ---- */

/* $E9F0 helmet: shield for player x, 10 units of 64 frames */
void powerup_helmet(uint8_t x) { ZPX(0x89, x) = 0x0A; }
void sub_E9F0(void) { powerup_helmet(X); A = 0x0A; SZN(A); }

/* $E9F5 timer: freeze all enemies, 10 units of 64 frames */
void powerup_timer(void) { enemy_freeze_timer = 0x0A; }
void sub_E9F5(void) { powerup_timer(); A = 0x0A; SZN(A); }

void build_eagle_concrete(uint8_t *yout);
/* $E9FB shovel: if the eagle is intact, wrap it in concrete for 20 units.
 * Returns X as the original leaves it (the queue index when it built). */
uint8_t powerup_shovel(uint8_t x) {
    if (base_state & 0x80) {
        uint8_t yo;
        build_eagle_concrete(&yo);
        shovel_timer = 0x14;
        return vram_queue_len;
    }
    return x;
}
void sub_E9FB(void) {
    uint8_t before = base_state;
    X = powerup_shovel(X);
    if (before & 0x80) { A = 0x14; SZN(A); } else { A = before; SZN(A); }
}

/* $EA07 star: player level +$20 up to $60; the tank type follows at once */
void powerup_star(uint8_t x) {
    uint8_t lv = RAM[0x101 + x];
    if (lv != 0x60) {
        lv = (uint8_t)(lv + 0x20);
        RAM[0x101 + x] = lv;
        ZPX(T_TYPE, x) = lv;
    }
}
void sub_EA07(void) {
    uint8_t before = RAM[0x101 + X];
    powerup_star(X);
    A = RAM[0x101 + X];
    if (before == 0x60) A = 0x60;
    SZN(A);
    fC = (before == 0x60) ? 1 : (uint8_t)((before + 0x20) > 0xFF);
}

/* $EA17 grenade: destroy every live enemy (slots 7..2) without score */
void powerup_grenade(void) {
    slot_idx = 7;
    sound_req[10] = 1;
    do {
        uint8_t y = slot_idx;
        if (tank_alive(RAM[T_STATE + y])) {
            RAM[T_STATE + y] = 0x73;
            RAM[T_TYPE + y] = 0;
        }
        slot_idx--;
    } while (slot_idx != 1);
}
void sub_EA17(void) { powerup_grenade(); Y = 1; A = 1; SZN(A); fC = 1; }

/* $EA3E extra life: one more life, sounds 4 and 5 */
void powerup_extra_life(uint8_t x) {
    ZPX(0x51, x)++;
    sound_req[4] = 1;
    sound_req[5] = 1;
}
void sub_EA3E(void) { powerup_extra_life(X); A = 1; SZN(A); }

/* $EA48: table entry 6, a bare RTS (unused power-up kind) */
void sub_EA48(void) { }

/* run the effect selected by tbl_powerup entry address h; returns X */
static uint8_t run_powerup_effect(uint16_t h, uint8_t x) {
    switch (h) {
    case 0xE9F0: powerup_helmet(x); break;
    case 0xE9F5: powerup_timer(); break;
    case 0xE9FB: x = powerup_shovel(x); break;
    case 0xEA07: powerup_star(x); break;
    case 0xEA17: powerup_grenade(); break;
    case 0xEA3E: powerup_extra_life(x); break;
    case 0xEA48: break;
    default: break;   /* not reachable: tbl_powerup has only these 7 entries */
    }
    return x;
}

/* $E972 powerup_pickup  (difftest: untestable, its pla/pla discards the
 * caller's return address; verified by lockstep through main_loop)
 * If a power-up is on the field and no collect flash is running, check the
 * players (slots 1,0): a live tank within 11 px on both axes collects it
 * (powerup_flash = $32). A real power-up ($00-$7F type) scores 500 (unless
 * demo), plays sound 6 and runs its effect through tbl_powerup.
 * QUIRK: the original then does PLA PLA and tail-jumps to the effect, so
 * the effect's RTS returns to the CALLER OF THE CALLER: main_loop skips
 * its remaining calls ($C310-$C319: game_over_slide, update_engine_sound,
 * draw_hud, palette_cycle) for that frame.
 * Returns 1 when that happened (caller must abandon its own continuation),
 * 0 when it returned normally. spawn_powerup calls it with type $FF, which
 * returns normally right after setting the flash. */
int powerup_pickup(uint8_t *xout) {
    if (powerup_x == 0) return 0;
    if (powerup_flash != 0) return 0;
    pickup_slot = 1;
    for (;;) {
        uint8_t x = pickup_slot;
        *xout = x;
        if (tank_alive(RAM[T_STATE + x]) &&
            abs8((uint8_t)(RAM[T_X + x] - powerup_x)) < 0x0C &&
            abs8((uint8_t)(RAM[T_Y + x] - powerup_y)) < 0x0C) {
            powerup_flash = 0x32;
            if (powerup_type & 0x80) return 0;
            if (game_mode != 2) {
                set_score_popup(0x50);
                add_digits(pickup_slot);
                check_extra_life();
                sound_req[6] = 1;
            }
            uint16_t h = ROM16(TBL_POWERUP + (uint8_t)(powerup_type << 1));
            ptr_lo = (uint8_t)h;
            ptr_hi = (uint8_t)(h >> 8);
            *xout = run_powerup_effect(h, pickup_slot);
            return 1;
        }
        pickup_slot--;
        if (pickup_slot & 0x80) return 0;
    }
}

/* Adapter for register-level callers (none in the official build): on an
 * abort the original leaves S two bytes higher, as register-level callers
 * detect it. */
void sub_E972(void) {
    uint8_t xo = X;   /* the early returns leave X unchanged */
    if (powerup_pickup(&xo)) S = (uint8_t)(S + 2);
    X = xo;
}

/* $E8BE spawn_powerup: a bonus enemy was hit. Pick a random spot on the
 * 4x4 grid of $30/$60/$90/$C0 not within the pickup window of a player
 * (powerup_pickup run with type $FF is the overlap test), then a random
 * kind from the 8-entry weighted table (star and grenade twice as likely).
 * Plays sound 9. Restores X = slot_idx, Y = cell_w for its caller. */
void spawn_powerup(void) {
    sound_req[9] = 1;
    uint8_t xo;
    do {
        powerup_x = powerup_coord(rng() & 3);
        powerup_y = powerup_coord(rng() & 3);
        powerup_type = 0xFF;
        powerup_flash = 0;
        powerup_pickup(&xo);       /* type $FF: always returns normally */
    } while (powerup_flash != 0);
    powerup_type = ROM8(TBL_POWERUP_KIND + (rng() & 7));
    powerup_flash = 0;
}
void sub_E8BE(void) {
    /* the first rng call needs X preserved; rng restores X itself */
    spawn_powerup();
    X = slot_idx;
    Y = cell_w;
    A = 0;
    SZN(Y);
}

/* ======================================================================
 * Bullet vs tank / bullet vs bullet
 * ====================================================================== */

/* $E70C bullet_vs_tank: three passes over (tank, bullet) pairs, hit window
 * |dx| < 10 and |dy| < 10 (a 19x19 box around the tank centre):
 *  1. enemy bullets (slots 2..7) vs players: bullet explodes; if the
 *     player has a shield the bullet just vanishes; otherwise the player
 *     is destroyed ($73), sound 7, level reset to 0.
 *  2. player bullets (0,1,8,9) vs enemies (slots 7..2): bullet explodes;
 *     a flashing (bonus) enemy drops a power-up; armour: enemy type low 2
 *     bits count down (sound 14); at 0 the enemy dies ($73, sound 10),
 *     the kill is counted for the bullet's owner (slot&1), score
 *     tbl_enemy_score[kind] (kind = type>>5 - 4) is awarded unless demo.
 *  3. player bullets vs the OTHER player: shielded -> bullet vanishes;
 *     otherwise the tank is frozen for $C8 frames (no friendly damage);
 *     no effect in demo mode. */
void bullet_vs_tank(void) {
    /* pass 1: enemy bullets -> players */
    slot_idx = 1;
    do {
        uint8_t x = slot_idx;
        if (tank_alive(RAM[T_STATE + x])) {
            cell_w = 7;
            do {
                uint8_t y = cell_w;
                if ((RAM[B_STATE + y] & 0xF0) == 0x40 &&
                    abs8((uint8_t)(RAM[B_X + y] - RAM[T_X + x])) < 0x0A &&
                    abs8((uint8_t)(RAM[B_Y + y] - RAM[T_Y + x])) < 0x0A) {
                    RAM[B_STATE + y] = 0x33;
                    if (player_shield[x] != 0) {
                        RAM[B_STATE + y] = 0;
                    } else {
                        RAM[T_STATE + x] = 0x73;
                        sound_req[7] = 1;
                        RAM[0x101 + x] = 0;      /* player_level */
                        RAM[T_TYPE + x] = 0;
                        break;
                    }
                }
                cell_w--;
            } while (cell_w != 1);
        }
        slot_idx--;
    } while (!(slot_idx & 0x80));

    /* pass 2: player bullets -> enemies */
    slot_idx = 7;
    do {
        uint8_t x = slot_idx;
        if (tank_alive(RAM[T_STATE + x])) {
            cell_w = 9;
            do {
                uint8_t y = cell_w;
                if (!(y & 6) && (RAM[B_STATE + y] & 0xF0) == 0x40 &&
                    abs8((uint8_t)(RAM[B_X + y] - RAM[T_X + x])) < 0x0A &&
                    abs8((uint8_t)(RAM[B_Y + y] - RAM[T_Y + x])) < 0x0A) {
                    RAM[B_STATE + y] = 0x33;
                    if (RAM[T_TYPE + x] & 0x04) {        /* flashing bonus enemy */
                        spawn_powerup();
                        if (RAM[T_TYPE + x] == 0xE4) RAM[T_TYPE + x]--;
                    }
                    if (RAM[T_TYPE + x] & 0x03) {        /* armour left */
                        RAM[T_TYPE + x]--;
                        sound_req[14] = 1;
                    } else {
                        RAM[T_STATE + x] = 0x73;
                        sound_req[10] = 1;
                        uint8_t kind = (uint8_t)((RAM[T_TYPE + x] >> 5) - 4);
                        cell_x = cell_w & 1;             /* owner player */
                        if (cell_x == 0) p1_kills[kind]++; else p2_kills[kind]++;
                        if (game_mode != 2) {
                            set_score_popup(ROM8(TBL_ENEMY_SCORE + kind));
                            add_digits(cell_x);
                            check_extra_life();
                        }
                        break;
                    }
                }
                cell_w--;
            } while (!(cell_w & 0x80));
        }
        slot_idx--;
    } while (slot_idx != 1);

    /* pass 3: player bullets -> the other player (stun) */
    slot_idx = 1;
    do {
        uint8_t x = slot_idx;
        if (tank_alive(RAM[T_STATE + x])) {
            cell_w = 9;
            do {
                uint8_t y = cell_w;
                if (!(y & 6) && (RAM[B_STATE + y] & 0xF0) == 0x40 &&
                    ((slot_idx ^ cell_w) & 1) &&
                    abs8((uint8_t)(RAM[B_X + y] - RAM[T_X + x])) < 0x0A &&
                    abs8((uint8_t)(RAM[B_Y + y] - RAM[T_Y + x])) < 0x0A) {
                    RAM[B_STATE + y] = 0x33;
                    if (player_shield[x] != 0) {
                        RAM[B_STATE + y] = 0;
                    } else if (tank_freeze[x] == 0 && game_mode != 2) {
                        tank_freeze[x] = 0xC8;
                        break;
                    }
                }
                cell_w--;
            } while (!(cell_w & 0x80));
        }
        slot_idx--;
    } while (!(slot_idx & 0x80));
}
void sub_E70C(void) { bullet_vs_tank(); X = 0; A = 0; SZN(A); }

/* $E910 bullet_vs_bullet: each player bullet (0,1,8,9) in flight cancels
 * any other flying bullet within |dx|<6 and |dy|<6 (11x11 window), both
 * vanish without explosion. The two bullets of the same player (slot&7
 * equal, i.e. 0/8 or 1/9) ignore each other.
 * QUIRK: after one pair is cancelled the scan continues without
 * re-checking that bullet x is still flying, so one player bullet can
 * cancel several others in the same pass. */
void bullet_vs_bullet(void) {
    slot_idx = 9;
    do {
        uint8_t x = slot_idx;
        if (!(x & 6) && (RAM[B_STATE + x] & 0xF0) == 0x40) {
            cell_w = 9;
            do {
                uint8_t y = cell_w;
                scratch_00 = y & 7;
                if ((slot_idx & 7) != scratch_00 &&
                    (RAM[B_STATE + y] & 0xF0) == 0x40 &&
                    abs8((uint8_t)(RAM[B_X + y] - RAM[B_X + x])) < 6 &&
                    abs8((uint8_t)(RAM[B_Y + y] - RAM[B_Y + x])) < 6) {
                    RAM[B_STATE + x] = 0;
                    RAM[B_STATE + y] = 0;
                }
                cell_w--;
            } while (!(cell_w & 0x80));
        }
        slot_idx--;
    } while (!(slot_idx & 0x80));
}
void sub_E910(void) { bullet_vs_bullet(); X = 0xFF; A = 0xFF; SZN(A); }

/* ======================================================================
 * Eagle walls, shovel, eagle explosion
 * ====================================================================== */

/* draw an $FF-terminated tile string at ROM address str to cell (col,row)
 * (the original loads the address into ptr $11/$12 first) */
static uint8_t draw_rom_string(uint16_t str, uint8_t col, uint8_t row) {
    ptr_hi = (uint8_t)(str >> 8);
    ptr_lo = (uint8_t)str;
    return draw_tiles(col, row);
}

/* queue the 4-byte attribute update record for $23F3 (the byte covering the
 * eagle's surround): data bytes b1 (for $23F3) -- store into the shadow */
static void queue_eagle_attr(uint8_t b1, int mask_second, uint8_t and_mask, uint8_t or_mask) {
    uint8_t q = vram_queue_len;
    vq_put(&q, 0x23);
    vq_put(&q, 0xF3);
    RAM[0x7F3] = b1;
    vq_put(&q, b1);
    (void)mask_second;
    uint8_t v = (uint8_t)((RAM[0x7F4] & and_mask) | or_mask);
    RAM[0x7F4] = v;
    vq_put(&q, v);
    vq_put(&q, 0xFF);
    vram_queue_len = q;
}

/* $CAF5 build_eagle_brick: the brick wall around the eagle (rows 24..27,
 * 4 strings at $D36D,$D374,$D37B,$D382) and its attribute bytes. Also
 * used to rebuild the wall when the shovel expires. */
void build_eagle_brick(uint8_t *yout) {
    uint8_t y = 0;
    y = draw_rom_string(0xD36D, 0x0C, 0x18);
    y = draw_rom_string(0xD374, 0x0C, 0x19);
    y = draw_rom_string(0xD37B, 0x0C, 0x1A);
    y = draw_rom_string(0xD382, 0x0C, 0x1B);
    queue_eagle_attr(0x00, 1, 0xCC, 0x00);
    *yout = y;
}
void sub_CAF5(void) { uint8_t y; build_eagle_brick(&y); Y = y; X = vram_queue_len; A = 0xFF; SZN(A); }

/* $CB9E build_eagle_concrete: the shovel's concrete wall
 * (strings $D389,$D390,$D397,$D39E), attributes $3F / ($F4&$CC|$33). */
void build_eagle_concrete(uint8_t *yout) {
    uint8_t y = 0;
    y = draw_rom_string(0xD389, 0x0C, 0x18);
    y = draw_rom_string(0xD390, 0x0C, 0x19);
    y = draw_rom_string(0xD397, 0x0C, 0x1A);
    y = draw_rom_string(0xD39E, 0x0C, 0x1B);
    queue_eagle_attr(0x3F, 1, 0xCC, 0x33);
    *yout = y;
}
void sub_CB9E(void) { uint8_t y; build_eagle_concrete(&y); Y = y; X = vram_queue_len; A = 0xFF; SZN(A); }

/* $CC08 draw the destroyed eagle (two 3-tile strings at $D3AB/$D3AE,
 * rows 26,27, column 14). Returns Y (length of the last string). */
uint8_t draw_destroyed_eagle(void) {
    draw_rom_string(0xD3AB, 0x0E, 0x1A);
    return draw_rom_string(0xD3AE, 0x0E, 0x1B);
}
void sub_CC08(void) { Y = draw_destroyed_eagle(); A = 0; SZN(Y); }

/* ---- eagle explosion animation ($E312..$E33E) ---- */

/* $E328: draw a 16x16 sprite with tile A + explosion_tile_offset at (x,y) */
void draw_eagle_part(uint8_t tile, uint8_t x, uint8_t y) {
    sprite_tile = tile;
    draw_sprite16_at(x, y);
}
void sub_E328(void) { draw_eagle_part((uint8_t)(A + explosion_tile_offset), X, Y); }

/* $E312/$E317/$E31C: small explosion frames $F1/$F5/$F9 at the eagle */
void eagle_small_boom(uint8_t tile) { draw_eagle_part(tile, 0x78, 0xD8); }
void sub_E312(void) { eagle_small_boom(0xF1); }
void sub_E317(void) { eagle_small_boom(0xF5); }
void sub_E31C(void) { eagle_small_boom(0xF9); }

/* $E33E: big explosion, four 16x16 quadrants around (128,208) */
void eagle_big_boom(void) {
    draw_eagle_part((uint8_t)(0xD1 + explosion_tile_offset), 0x70, 0xD0);
    draw_eagle_part((uint8_t)(0xD5 + explosion_tile_offset), 0x80, 0xD0);
    draw_eagle_part((uint8_t)(0xD9 + explosion_tile_offset), 0x70, 0xE0);
    draw_eagle_part((uint8_t)(0xDD + explosion_tile_offset), 0x80, 0xE0);
}
void sub_E33E(void) { eagle_big_boom(); }
void sub_E32E(void) { explosion_tile_offset = 0x00; eagle_big_boom(); }
void sub_E336(void) { explosion_tile_offset = 0x10; eagle_big_boom(); }

/* $E2A9 shovel_and_base: (1) tick the shovel timer every 64 frames; when
 * it runs low (< 4 units) the concrete wall flashes between concrete and
 * brick every 16 frames, and when it hits 0 the brick wall is rebuilt.
 * (2) Eagle explosion: while base_state is 1..$7F it counts down once per
 * frame from $27 and picks a frame by |(|state/4 - 5|) - 5|: small boom
 * x3, big boom x2 (second with tile offset $10), then reversed. */
void shovel_and_base(void) {
    if (shovel_timer != 0 && (frame_counter & 0x0F) == 0) {
        int brick = 0, concrete = 0, check = 1;
        if ((frame_counter & 0x3F) == 0) {
            shovel_timer--;
            if (shovel_timer == 0) { brick = 1; check = 0; }
        }
        if (check && shovel_timer < 4) {
            if (frame_counter & 0x10) concrete = 1; else brick = 1;
        }
        uint8_t yo;
        if (brick) build_eagle_brick(&yo);
        else if (concrete) build_eagle_concrete(&yo);
    }
    if (base_state == 0 || (base_state & 0x80)) return;
    scratch_04 = 3;
    base_state--;
    uint8_t v = (uint8_t)((base_state >> 2) - 5);
    v = abs8(v);
    v = (uint8_t)(v - 5);
    v = abs8(v);
    uint8_t idx = (uint8_t)(v << 1);
    uint16_t h = ROM16(TBL_EAGLE_ANIM + idx);
    ptr_lo = (uint8_t)h;
    ptr_hi = (uint8_t)(h >> 8);
    switch (h) {
    case 0xDBF0: break;
    case 0xE312: eagle_small_boom(0xF1); break;
    case 0xE317: eagle_small_boom(0xF5); break;
    case 0xE31C: eagle_small_boom(0xF9); break;
    case 0xE32E: explosion_tile_offset = 0x00; eagle_big_boom(); break;
    case 0xE336: explosion_tile_offset = 0x10; eagle_big_boom(); break;
    default: break;   /* unreachable: idx is 0..10, within the 6-entry table, all known */
    }
}
void sub_E2A9(void) { shovel_and_base(); A = 0; SZN(A); }
