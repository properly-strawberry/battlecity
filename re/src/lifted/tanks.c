/* Tanks: movement, player control, enemy AI, spawning, terrain occupancy, RNG
 * ($D44D, $DAAF, $DB0B-$DEA5, $E181-$E23A, $E363-$E46B).
 *
 * Model (verified by these routines):
 *  - Eight tank slots: 0/1 = players, 2..7 = enemies. Per-slot arrays
 *    tank_x/tank_y (pixel position, 16x16 tank), tank_state, tank_type,
 *    tank_tread (animation flip), tank_map_lo/hi (cached terrain pointer).
 *  - tank_state: bit 7 = active, high nibble = MODE, low 2 bits = facing
 *    (0 up, 1 left, 2 down, 3 right; tbl_dx/tbl_dy), bit 3 of a mode-8 state
 *    is a turn delay counter. move_tank dispatches on the mode:
 *       0   free slot (nothing)
 *       1-7 dying: explosion animation counting down, then respawn / free
 *       8   turning/idle: counts down 4 per frame, then becomes mode A
 *       9   enemy AI: random turn (or pick_direction)
 *       A   driving: step one pixel in the facing direction
 *       B   seek the eagle (sets facing, becomes mode A)
 *       C   seek player 2        D  seek player 1
 *       E   spawn twinkle phase 2 (14 frames), then init_tank_type
 *       F   spawn twinkle phase 1 (14 frames), then mode E
 *  - Terrain cells are tile numbers; bit 7 of a cell is borrowed as an
 *    "occupied by a tank" mark (set by update_tank_terrain, cleared by
 *    clear_tank_occupancy). Movement treats cells with bit 7 as solid, as
 *    well as tiles $01-$1F (bricks, concrete, water-like blockers). */
#include "common.h"

#include "lifted.h"

/* ROM tables: names and addresses in src/tables.h */

/* zero-page,X indexing wraps within page 0; name_at(i) = name[i] with that wrap */
#define ZP(base, i) RAM[(uint8_t)((base) + (i))]
#define tank_x_at(i) ZP(0x90, i)
#define tank_y_at(i) ZP(0x98, i)
#define tank_state_at(i) ZP(0xA0, i)
#define tank_type_at(i) ZP(0xA8, i)
#define tank_b0_at(i) ZP(0xB0, i)
#define bullet_state_at(i) ZP(0xCC, i)
#define tank_map_lo_at(i) ZP(0xE0, i)
#define tank_map_hi_at(i) ZP(0xE8, i)
#define joy_held_at(i) ZP(0x06, i)
#define player_lives_at(i) ZP(0x51, i)
#define tank_freeze_at(i) ZP(0x6F, i)
#define player_shield_at(i) ZP(0x89, i)
#define player_slide_at(x) RAM[0x103 + (x)]   /* index may run past the 2 player bytes */

/* ------------------------------------------------------------------ */
/* $D44D rng: pseudo random byte.
 * state = state*7 + phase_timer + zp[++rng_stir] + carry. The mixed-in byte
 * is whatever zero-page byte rng_stir now points at (so the sequence
 * depends on game state). X is preserved. Returns the new state. */
uint8_t rng_core(uint8_t *carry_out, uint8_t *ovf_out) {
    uint8_t s = rng_state;
    uint8_t a = (uint8_t)(s << 3);
    unsigned d = (unsigned)a + (uint8_t)~s + 1;          /* sec; sbc state */
    a = (uint8_t)d;
    unsigned t = (unsigned)a + phase_timer;                /* clc; adc phase_timer */
    uint8_t c1 = t > 0xFF;
    uint8_t a1 = (uint8_t)t;
    rng_stir++;
    uint8_t mix = RAM[rng_stir];                           /* zp,X: wraps in page 0 */
    unsigned u = (unsigned)a1 + mix + c1;                  /* adc scratch_00,x */
    *ovf_out = ((~(a1 ^ mix) & (a1 ^ u)) >> 7) & 1;
    *carry_out = u > 0xFF;
    rng_state = (uint8_t)u;
    return rng_state;
}
uint8_t rng(void) { uint8_t c, v; return rng_core(&c, &v); }
void sub_D44D(void) {
    uint8_t c, v;
    A = rng_core(&c, &v);
    fC = c; fV = v; SZN(A);
}

/* ------------------------------------------------------------------ */
/* $DAAF sign_of: -1/0/+1 of a subtraction result, from its flags
 * (Z = equal, C = no borrow, i.e. minuend > subtrahend when not Z). */
uint8_t sign_of(uint8_t a, uint8_t zero, uint8_t carry) {
    if (zero) return a;
    return carry ? 0x01 : 0xFF;
}
void sub_DAAF(void) { A = sign_of(A, fZ, fC); }

/* $E451 decode_direction: d-pad bits (R=$80 L=$40 D=$20 U=$10) to facing
 * 0 up, 1 left, 2 down, 3 right; $FF if none. Priority right, left, down, up. */
uint8_t decode_direction(uint8_t buttons) {
    if (buttons & 0x80) return 3;
    if (buttons & 0x40) return 1;
    if (buttons & 0x20) return 2;
    if (buttons & 0x10) return 0;
    return 0xFF;
}
void sub_E451(void) { A = decode_direction(A); SZN(A); }

/* $E420 set_tank_mode: replace the high nibble of tank_state_at(x) with mode
 * (already shifted: $80, $A0, ...). Leaves mode in scratch_00. */
uint8_t set_tank_mode(uint8_t mode, uint8_t x) {
    scratch_00 = mode;
    tank_state_at(x) = (uint8_t)((tank_state_at(x) & 0x0F) | mode);
    return tank_state_at(x);
}
void sub_E420(void) { A = set_tank_mode(A, X); SZN(A); }

/* ------------------------------------------------------------------ */
/* $DB38 player_is_driving: 1 if player x holds a direction and is alive. */
uint8_t player_is_driving(uint8_t x) {
    if ((joy_held_at(x) & 0xF0) && tank_state_at(x)) return 1;
    return 0;
}
void sub_DB38(void) { A = player_is_driving(X); SZN(A); }

/* $DB0B update_engine_sound: sound_req[17] (engine loop) is on while either
 * player is driving. */
void update_engine_sound(void) {
    if (sound_req[17] != 0) {
        if (player_is_driving(0)) return;
        if (player_is_driving(1)) return;
        sound_req[17] = 0;
    } else {
        if (player_is_driving(0) || player_is_driving(1)) sound_req[17] = 1;
    }
}
void sub_DB0B(void) { update_engine_sound(); }

/* ------------------------------------------------------------------ */
/* $E3B8 init_tank_type: a tank leaving its spawn twinkle becomes live:
 * facing from tbl_slot_facing, type from player_level (players, with a
 * 3*64-frame shield) or the next enemy kind of the stage (enemies). */
void init_tank_type(uint8_t x) {
    uint8_t a;
    tank_state_at(x) = ROM8(TBL_SLOT_FACING + x);
    if (x < 2) {
        player_shield_at(x) = 3;
        a = player_level[x];
    } else {
        uint8_t y = enemy_type_idx;
        /* skip exhausted enemy kinds (abs,Y read, may wrap into other RAM) */
        while (RAM[(0x8B + y) & 0x7FF] == 0) {
            enemy_type_idx++;
            y = enemy_type_idx;
        }
        RAM[(0x8B + y) & 0x7FF] = (uint8_t)(RAM[(0x8B + y) & 0x7FF] - 1);
        uint8_t level = game_mode ? 0x23 : stage_number;
        uint8_t idx = (uint8_t)(((uint8_t)(level - 1) << 2) + enemy_type_idx);
        a = ROM8(TBL_ENEMY_TYPE + idx);
        if (a == 0xE0) a |= 0x03;   /* armoured (type E) tanks start at HP 3 */
    }
    a |= tank_type_at(x);              /* bit 2 = flashing bonus tank */
    if (a == 0xE7) a = 0xE4;        /* QUIRK: bonus armoured tank with HP bits 3 is renormalised to 4 */
    tank_type_at(x) = a;
    tank_b0_at(x) = 0;
}
void sub_E3B8(void) { init_tank_type(X); A = 0; SZN(A); }

/* $E363 spawn_tank: put tank x on its spawn point and start the twinkle
 * (state $F0). Players use fixed points; enemies rotate through the three
 * top points, and the 3rd, 10th and 17th-from-last enemy (enemies_to_spawn
 * = 3, $0A, $11) are flashing bonus tanks (tank_type 4, powerup cleared). */
void spawn_tank(uint8_t x) {
    tank_type_at(x) = 0;
    if (x < 2) {
        tank_x_at(x) = ROM8(PLAYER_SPAWN_X + x);
        tank_y_at(x) = ROM8(PLAYER_SPAWN_Y + x);
        tank_freeze_at(x) = 0;
    } else {
        spawn_point_idx++;
        uint8_t y = spawn_point_idx;
        if (y == 3) { spawn_point_idx = 0; y = 0; }
        tank_x_at(x) = ROM8(ENEMY_SPAWN_X + y);
        tank_y_at(x) = ROM8(ENEMY_SPAWN_Y + y);
        if (enemies_to_spawn == 0x03 || enemies_to_spawn == 0x0A || enemies_to_spawn == 0x11) {
            tank_type_at(x) = 0x04;
            powerup_x = 0;
        }
    }
    tank_state_at(x) = 0xF0;
    draw_block(0x0F, tank_x_at(x), tank_y_at(x));   /* $D80B: tile pattern $0F at the spawn */
}
void sub_E363(void) { spawn_tank(X); }

/* $DB48 spawn_enemy: every ai_clock frames release the next enemy into the
 * highest free slot (max_tank_slot down to 2) and update the HUD enemy count. */
void spawn_enemy(void) {
    if (spawn_cooldown != 0) { spawn_cooldown--; return; }
    if (enemies_to_spawn == 0) return;
    slot_idx = max_tank_slot;
    do {
        uint8_t x = slot_idx;
        if (tank_state_at(x) == 0) {
            spawn_cooldown = ai_clock;
            spawn_tank(x);
            enemies_to_spawn--;
            erase_enemy_icon(enemies_to_spawn);   /* $C8B1 */
            return;
        }
        slot_idx--;
    } while (slot_idx != 1);
}
void sub_DB48(void) { spawn_enemy(); }

/* ------------------------------------------------------------------ */
/* $DB75 player_control: input to facing. Runs on 3 of every 4 frames
 * (odd frames and frames divisible by 4). A frozen (stunned) player or one
 * skidding on ice (slide bit 4) ignores the pad; a player with no direction
 * enters the idle mode 8. A 90 degree turn snaps the tank to the 8 px grid;
 * a 180 degree reversal does not. Ice: when the slide counter low bits are 0
 * it is re-armed to $9C with a sound. */
void player_control(void) {
    if (!(frame_counter & 1) && (frame_counter & 3)) return;
    for (int8_t i = 1; i >= 0; i--) {
        uint8_t x = (uint8_t)i;
        uint8_t st = tank_state_at(x);
        if (!(st & 0x80)) continue;
        if (st >= 0xE0) continue;               /* still spawning */
        int stop = 0;
        if (tank_freeze_at(x)) {
            tank_freeze_at(x)--;
            stop = 1;
        } else {
            uint8_t sl = player_slide_at(x);
            if ((sl & 0x80) && (sl & 0x10)) stop = 1;
        }
        if (!stop) {
            uint8_t dir = decode_direction(joy_held_at(x));
            scratch_00 = dir;
            if (dir & 0x80) stop = 1;
        }
        if (stop) {
            set_tank_mode(0x80, x);
            tank_state_at(x) |= 0x08;
            continue;
        }
        uint8_t dir = scratch_00;
        uint8_t sl = player_slide_at(x);
        if ((sl & 0x80) && (sl & 0x1F) == 0) {
            player_slide_at(x) = 0x9C;
            sound_req[16] = 1;
        }
        uint8_t face = tank_state_at(x) & 3;
        if (face != dir && (face ^ 2) != dir) {
            tank_x_at(x) = (uint8_t)((tank_x_at(x) + 4) & 0xF8);
            tank_y_at(x) = (uint8_t)((tank_y_at(x) + 4) & 0xF8);
        }
        tank_state_at(x) = (uint8_t)(dir | 0xA0);
    }
}
void sub_DB75(void) { player_control(); }
void sub_DBF0(void) { }   /* shared bare RTS: dispatch target for mode 0 */

/* ------------------------------------------------------------------ */
/* $DD6E clamp_probe_x / $DD76 clamp_probe_y: a leading-corner probe is an
 * inclusive pixel; when it is >= the tank position make it exclusive. */
uint8_t clamp_probe_x(uint8_t v) { return v >= tmp_x ? (uint8_t)(v - 1) : v; }
void sub_DD6E(void) { A = clamp_probe_x(A); fC = 0; SZN(A); }
uint8_t clamp_probe_y(uint8_t v) { return v >= tmp_y ? (uint8_t)(v - 1) : v; }
void sub_DD76(void) { A = clamp_probe_y(A); fC = 0; SZN(A); }

/* a probed terrain cell blocks movement when occupied (bit 7) or a
 * solid tile $01-$1F */
static int cell_blocks(uint8_t v) {
    if (v & 0x80) return 1;
    return v != 0 && v < 0x20;
}

/* terrain cell under pixel (px,py) via coords_to_map (leaves ptr set) */
static uint8_t probe_cell(uint8_t px, uint8_t py) {
    coords_to_map(px, py);
    return ind_y(0x11, 0);          /* $D706 leaves Y = 0 */
}

/* $DD30 tank_reverse: turn around (facing ^ 2); on the 8 px grid also
 * enter mode 9 so the AI picks again. */
static void tank_reverse(uint8_t x) {
    if ((tank_x_at(x) & 7) == 0 && (tank_y_at(x) & 7) == 0) set_tank_mode(0x90, x);
    tank_state_at(x) ^= 0x02;
}

/* body of $DC7C from $DC97: advance the tank one pixel along its facing if
 * both leading corners are free, else stop/turn. Players and enemies share
 * it. Flips the animation bit tank_tread bit 2 every call (tread animation). */
static void tank_step(uint8_t x0) {
    uint8_t face = tank_state_at(x0) & 3;
    uint8_t dy = ROM8(TBL_DY + face), dx = ROM8(TBL_DX + face);
    probe_dy8 = (uint8_t)(dy << 3);
    tmp_y = (uint8_t)(dy + tank_y_at(x0));
    probe_dx8 = (uint8_t)(dx << 3);
    tmp_x = (uint8_t)(dx + tank_x_at(x0));

    uint8_t px = clamp_probe_x((uint8_t)(tmp_x + probe_dx8 + probe_dy8));
    uint8_t py = clamp_probe_y((uint8_t)(tmp_y + probe_dx8 + probe_dy8));
    int blocked = cell_blocks(probe_cell(px, py));
    if (!blocked) {
        px = clamp_probe_x((uint8_t)(tmp_x + probe_dx8 - probe_dy8));
        py = clamp_probe_y((uint8_t)(tmp_y + probe_dy8 - probe_dx8));
        blocked = cell_blocks(probe_cell(px, py));
    }
    uint8_t x = slot_idx;           /* the original reloads X from slot_idx */
    if (!blocked) {
        tank_x_at(x) = tmp_x;
        tank_y_at(x) = tmp_y;
    } else if (x >= 2) {
        if ((rng() & 3) == 0) { tank_reverse(x); return; }   /* 1 in 4: turn around */
        set_tank_mode(0x80, x);                              /* else idle-turn, AI re-picks */
        tank_state_at(x) |= 0x08;
    }
    tank_b0_at(x) ^= 0x04;
}

/* ------------------------------------------------------------------ */
/* $DE72 pick_direction: choose the enemy's next behaviour from the
 * stage clock. phase_timer (+1 per 64 frames) selects the phase:
 *   phase_timer <= ai_clock/8        random facing (mode A)
 *   ai_clock/8 < pt <= ai_clock/4    hunt a player (mode C or D)
 *   pt > ai_clock/4                  head for the eagle (mode B)
 * Even slots hunt player 1, odd slots player 2; if one player is dead
 * the other is hunted. */
void pick_direction(uint8_t x) {
    uint8_t a = (uint8_t)(ai_clock >> 2);
    if (a < phase_timer) { set_tank_mode(0xB0, x); return; }
    a >>= 1;
    if (a >= phase_timer) {
        tank_state_at(x) = (uint8_t)((rng() & 3) | 0xA0);
        return;
    }
    uint8_t mode;
    if (tank_state[0] == 0) mode = 0xC0;
    else if ((x & 1) == 0) mode = 0xD0;
    else if (tank_state[1] == 0) mode = 0xD0;
    else mode = 0xC0;
    set_tank_mode(mode, x);
}
void sub_DE72(void) { pick_direction(X); A = tank_state_at(X); fC = 0; SZN(A); }

/* $DC7C mode A (driving). Enemies on an 8 px grid point turn by pick_direction
 * with chance 1/16 per frame, otherwise step. */
void tank_drive(uint8_t x) {
    if (x >= 2 && (tank_x_at(x) & 7) == 0 && (tank_y_at(x) & 7) == 0) {
        if ((rng() & 0x0F) == 0) { pick_direction(x); return; }
    }
    tank_step(x);
}
void sub_DC7C(void) { tank_drive(X); }

/* $DC52 mode 8 (turning / idle). A player on ice with a non-zero slide
 * counter keeps sliding (steps forward, counter--). Otherwise state -= 4
 * each frame; when the delay bits ($0C) reach 0 the tank returns to mode A. */
void tank_turning(uint8_t x) {
    if (x < 2) {
        uint8_t sl = player_slide_at(x);
        if ((sl & 0x80) && (sl & 0x7F) != 0) {
            player_slide_at(x) = (uint8_t)(sl - 1);
            tank_b0_at(x) ^= 0x04;
            tank_step(x);
            return;
        }
    }
    tank_state_at(x) = (uint8_t)(tank_state_at(x) - 4);
    if ((tank_state_at(x) & 0x0C) == 0) set_tank_mode(0xA0, x);
}
void sub_DC52(void) { tank_turning(X); }

/* $DD48 mode 9: enemy random turn. 50%: pick_direction; else rotate facing
 * +-1 (left/right 90 degrees) and drive. */
void tank_random_turn(uint8_t x) {
    if ((rng() & 1) == 0) { pick_direction(x); return; }
    uint8_t v;
    if (rng() & 1) v = (uint8_t)(tank_state_at(x) + 1);
    else v = (uint8_t)(tank_state_at(x) - 1);
    tank_state_at(x) = (uint8_t)((v & 3) | 0xA0);
}
void sub_DD48(void) { tank_random_turn(X); }

/* $DDA2 heading_to_target: facing ($A0-$A3) to move from tank x toward
 * (seek_x, seek_y). sign(dx)+1 and sign(dy)+1 index a 3x3 grid (3*sy+sx);
 * there are two grids (prefer vertical / prefer horizontal, +9). Enemies pick
 * the grid by rng; players' choice alternates with phase_timer. */
uint8_t heading_to_target(uint8_t x) {
    uint8_t sx = (uint8_t)(sign_of((uint8_t)(seek_x - tank_x_at(x)), seek_x == tank_x_at(x), seek_x >= tank_x_at(x)) + 1);
    scratch_64 = sx;
    uint8_t sy = (uint8_t)(sign_of((uint8_t)(seek_y - tank_y_at(x)), seek_y == tank_y_at(x), seek_y >= tank_y_at(x)) + 1);
    scratch_65 = sy;
    uint8_t idx = (uint8_t)(sy * 2 + sy + sx);
    scratch_64 = idx;
    uint8_t alt;
    if (x < 2) alt = ((uint8_t)(x << 1) ^ phase_timer) & 2;
    else alt = rng() & 1;
    uint8_t y = alt ? (uint8_t)(9 + idx) : idx;
    return ROM8(TBL_HEADING + y);
}
void sub_DDA2(void) { A = heading_to_target(X); }

/* $DD7E / $DD89 / $DD94 seek modes: target player 1 / player 2 / the eagle
 * ($78,$D8), then set the facing (state := $A0-$A3). */
void seek_player1(uint8_t x) { seek_x = tank_x[0]; seek_y = tank_y[0]; tank_state_at(x) = heading_to_target(x); }
void seek_player2(uint8_t x) { seek_x = tank_x[1]; seek_y = tank_y[1]; tank_state_at(x) = heading_to_target(x); }
void seek_eagle(uint8_t x)   { seek_x = 0x78; seek_y = 0xD8; tank_state_at(x) = heading_to_target(x); }
void sub_DD7E(void) { seek_player1(X); }
void sub_DD89(void) { seek_player2(X); }
void sub_DD94(void) { seek_eagle(X); }

/* ------------------------------------------------------------------ */
/* $DE46 arm_game_over_banner: start the per-player banner at the bottom. */
void arm_game_over_banner(void) {
    game_over_timer = 0x0D;
    game_over_y = 0xD8;
    frame_counter = 0;
}
void sub_DE46(void) { arm_game_over_banner(); A = 0; SZN(A); }

/* $DDEA modes 1-7: explosion countdown. The low nibble counts down; at 0 the
 * mode steps down ($70 -> $60 ...). The last step frees the slot: a player
 * loses a life (respawn if any left, otherwise maybe the "game over" banner
 * for that player when the eagle still stands and the other player lives),
 * an enemy decrements enemies_left. */
void tank_dying(uint8_t x) {
    tank_state_at(x)--;
    if ((tank_state_at(x) & 0x0F) != 0) return;
    uint8_t v = (uint8_t)(tank_state_at(x) - 0x10);
    if (v != 0) {
        v |= (v == 0x10) ? 0x06 : 0x03;
        tank_state_at(x) = v;
        return;
    }
    tank_state_at(x) = 0;
    if (x >= 2) { enemies_left--; return; }
    player_lives_at(x)--;
    if (player_lives_at(x) != 0) { spawn_tank(x); return; }
    if (base_state != 0x80) return;
    if (x == 1) {
        if (player_lives[0] == 0) return;
        game_over_dir = 1; game_over_x = 0xC0;
    } else {
        if (player_lives[1] == 0) return;
        game_over_dir = 3; game_over_x = 0x20;
    }
    arm_game_over_banner();
}
void sub_DDEA(void) { tank_dying(X); }

/* $DE55 mode F (spawn twinkle 1) / $DE64 mode E (spawn twinkle 2): the low
 * nibble counts up to 14, then F -> E, and E -> live tank (init_tank_type). */
void tank_spawn_phase1(uint8_t x) {
    tank_state_at(x)++;
    if ((tank_state_at(x) & 0x0F) == 0x0E) tank_state_at(x) = 0xE0;
}
void sub_DE55(void) { tank_spawn_phase1(X); }
void tank_spawn_phase2(uint8_t x) {
    tank_state_at(x)++;
    if ((tank_state_at(x) & 0x0F) == 0x0E) init_tank_type(x);
}
void sub_DE64(void) { tank_spawn_phase2(X); }

/* $DC3D move_tank: jump through tbl_state_handlers[state >> 4]. The table
 * is ROM data; the handler address is also left in ptr($11) and Y = index. */
void move_tank(uint8_t x) {
    uint8_t idx = (uint8_t)(((tank_state_at(x) >> 3) & 0xFE));
    uint16_t target = (uint16_t)(ROM8(TBL_STATE_HANDLERS + idx) | (ROM8(TBL_STATE_HANDLERS + 1 + idx) << 8));
    ptr_lo = (uint8_t)target;
    ptr_hi = (uint8_t)(target >> 8);
    switch (target) {
    case 0xDBF0: break;
    case 0xDDEA: tank_dying(x); break;
    case 0xDC52: tank_turning(x); break;
    case 0xDD48: tank_random_turn(x); break;
    case 0xDC7C: tank_drive(x); break;
    case 0xDD94: seek_eagle(x); break;
    case 0xDD89: seek_player2(x); break;
    case 0xDD7E: seek_player1(x); break;
    case 0xDE64: tank_spawn_phase2(x); break;
    case 0xDE55: tank_spawn_phase1(x); break;
    default: break;
    }
}
void sub_DC3D(void) {
    uint8_t idx = (uint8_t)((tank_state_at(X) >> 3) & 0xFE);
    move_tank(X);
    Y = idx;
}

/* $DBF1 move_all_tanks: speed gating, then move_tank for slots 7..0.
 *  - players move on 3 of 4 frames (same gate as player_control);
 *  - fast tanks (type $A0) move every frame, other enemies every second
 *    frame (slot ^ frame odd);
 *  - while enemy_freeze_timer (Timer powerup) is non-zero, live enemies
 *    ($80-$DF) are skipped; the timer drops once per 64 frames. */
void move_all_tanks(void) {
    slot_idx = 7;
    if (enemy_freeze_timer != 0 && (frame_counter & 0x3F) == 0) enemy_freeze_timer--;
    do {
        uint8_t x = slot_idx;
        int move;
        if (x < 2) {
            if (frame_counter & 1) move = 1;
            else move = ((frame_counter & 3) == 0);
        } else {
            move = 1;
            if (enemy_freeze_timer != 0) {
                uint8_t st = tank_state_at(x);
                if ((st & 0x80) && st < 0xE0) move = 0;
            }
            if (move) {
                if ((tank_type_at(x) & 0xF0) == 0xA0) move = 1;
                else move = ((slot_idx ^ frame_counter) & 1) != 0;
            }
        }
        if (move) move_tank(x);
        slot_idx--;
    } while (slot_idx < 0x80);
}
void sub_DBF1(void) { move_all_tanks(); }

/* ------------------------------------------------------------------ */
/* $E1F3 mark_cell_occupied / $E234 unmark_cell: set / clear bit 7 of the
 * terrain cell at ptr+y. */
uint8_t mark_cell_occupied(uint8_t y) { uint8_t v = ind_y(0x11, y) | 0x80; ind_y_store(0x11, y, v); return v; }
void sub_E1F3(void) { A = mark_cell_occupied(Y); SZN(A); }
uint8_t unmark_cell(uint8_t y) { uint8_t v = ind_y(0x11, y) & 0x7F; ind_y_store(0x11, y, v); return v; }
void sub_E234(void) { A = unmark_cell(Y); SZN(A); }

/* $E181 update_tank_terrain: for each live tank cache the terrain cell
 * pointer of (x-8,y-8) in tank_map_lo/hi (hi = ptr_hi & 3) and mark cells as
 * occupied: ptr+$21 always; ptr+$20 when x is on the 8 px grid (flagged by
 * tank_map_hi bit 7); ptr+$01 when y is on the grid (bit 6). Players also
 * set/clear player_slide bit 7 if the cell at ptr+$21 is ice (tile $21). */
void update_tank_terrain(void) {
    slot_idx = 7;
    do {
        uint8_t x = slot_idx;
        uint8_t st = tank_state_at(x);
        if ((st & 0x80) && st < 0xE0) {
            coords_to_map((uint8_t)(tank_x_at(x) - 8), (uint8_t)(tank_y_at(x) - 8));   /* $D706 */
            x = slot_idx;
            tank_map_lo_at(x) = ptr_lo;
            tank_map_hi_at(x) = ptr_hi & 0x03;
            if (x < 2) {
                if (ind_y(0x11, 0x21) == 0x21) player_slide_at(x) |= 0x80;
                else player_slide_at(x) &= 0x7F;
            }
            mark_cell_occupied(0x21);
            if ((tank_x_at(x) & 7) == 0) {
                tank_map_hi_at(x) |= 0x80;
                mark_cell_occupied(0x20);
            }
            if ((tank_y_at(x) & 7) == 0) {
                tank_map_hi_at(x) |= 0x40;
                mark_cell_occupied(0x01);
            }
        }
        slot_idx--;
    } while (slot_idx < 0x80);
}
void sub_E181(void) { update_tank_terrain(); A = 0; X = 0xFF; }

/* $E1FA clear_tank_occupancy: undo update_tank_terrain's marks using the
 * cached pointers (bits 7/6 of tank_map_hi say which extra cells). */
void clear_tank_occupancy(void) {
    slot_idx = 7;
    do {
        uint8_t x = slot_idx;
        uint8_t st = tank_state_at(x);
        if ((st & 0x80) && st < 0xE0) {
            ptr_lo = tank_map_lo_at(x);
            ptr_hi = (uint8_t)((tank_map_hi_at(x) & 0x03) | 0x04);
            unmark_cell(0x21);
            if (tank_map_hi_at(x) & 0x80) unmark_cell(0x20);
            if (tank_map_hi_at(x) & 0x40) unmark_cell(0x01);
        }
        slot_idx--;
    } while (slot_idx < 0x80);
}
void sub_E1FA(void) { clear_tank_occupancy(); A = 0; X = 0xFF; }

/* ------------------------------------------------------------------ */
/* $E409 clear_bullet_slots: bullet_state_at(0..9) = 0. */
void clear_bullet_slots(void) { for (int i = 9; i >= 0; i--) bullet_state_at(i) = 0; }
void sub_E409(void) { clear_bullet_slots(); A = 0; X = 0xFF; fZ = 0; fN = 1; }

/* $E413 clear_tank_slots: tank_state_at(0..7) = 0 and the bytes at $0103..$010A
 * = 0. QUIRK: player_slide only has 2 bytes; the loop also zeroes
 * $0105-$010A (game_over_x, game_over_y, game_over_dir, game_over_timer, ...). */
void clear_tank_slots(void) {
    for (int i = 7; i >= 0; i--) {
        tank_state_at(i) = 0;
        player_slide_at(i) = 0;
    }
}
void sub_E413(void) { clear_tank_slots(); A = 0; X = 0xFF; fZ = 0; fN = 1; }

/* $E42B load_enemy_counts: enemy_count[0..3] = the stage's four kind counts
 * from tbl_enemy_counts (stage*4); game_mode != 0 always uses row $23. */
void load_enemy_counts(void) {
    uint8_t level = game_mode ? 0x23 : stage_number;
    uint8_t y = (uint8_t)((uint8_t)(level - 1) << 2);
    for (int i = 0; i < 4; i++) enemy_count[i] = ROM8(TBL_ENEMY_COUNTS + y + i);
}
void sub_E42B(void) {
    uint8_t level = game_mode ? 0x23 : stage_number;
    load_enemy_counts();
    Y = (uint8_t)((uint8_t)(level - 1) << 2);
    A = enemy_count[3];
    SZN(A);
}
