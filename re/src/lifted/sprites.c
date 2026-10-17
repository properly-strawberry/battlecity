/* OAM sprite building ($DA2B-$DA92 core, tank/explosion/bullet/powerup/shield
 * drawing, banners, tally-screen tanks).
 *
 * Model (verified by these routines):
 *  - Sprites are written into oam_buffer ($0200). oam_index ($0D) is the
 *    next free byte; each sprite advances it by oam_step ($0E). The NMI-side
 *    routine $DA93 (not here) negates oam_step every frame and wipes the
 *    unused slots, so the draw order alternates direction and sprites
 *    flicker/drop alternately when more than 8 per scanline.
 *  - The game runs the PPU in 8x16 sprite mode. The tile byte's bit 0 picks
 *    the pattern table (all odd tiles here -> $1000 table, even -> $0000),
 *    and the 8x16 sprite shows tile N&$FE on top, N|1 below.
 *  - An 8x16 sprite is drawn by draw_sprite_oam ($DA2B). A "16x16 object" is
 *    two 8x16 sprites two tiles apart (draw_sprite16_at $DA7B): the left
 *    half at (x-8,y) with tile T and the right half at (x,y) with tile T+2.
 *    (x,y) is the object's centre-right/bottom point: OAM Y = y-8, X = x-8/x.
 *  - OAM attribute (scratch_04) is just the palette 0-3 here; $20 (behind
 *    background) is ORed in from behind_trees when the cell under the sprite
 *    is the tree tile $22.
 *  - Sprite palettes: 0 = player 1 (yellow), 1 = player 2 (green), 2 =
 *    enemies/bullets/powerups/shield/tally, 3 = explosions, banners,
 *    spawn twinkle, flashing (bonus) enemies alternate 2/3. (colours not
 *    checked, only the palette numbers.)
 *  - Zero-page names used for sprite state (ram.h): sprite_tile ($53, base tile
 *    of the sprite being built), sprite_attr ($04, attribute/palette),
 *    powerup_x ($86), scratch_54/scratch_55 = save slots for X/Y in $DA7B. */
#include "common.h"


/* from cells.c: point ptr ($11) at the map cell under pixel (px,py);
 * also leaves scratch_00 = (row&7)<<5. */
void coords_to_map(uint8_t px, uint8_t py);

/* Per-slot arrays are indexed zero-page,X: the index wraps within page 0
 * (matters only for out-of-range slots, kept for exactness). */
#define ZPX(base, i)    RAM[(uint8_t)((base) + (i))]
#define TANK_X(i)       ZPX(0x90, i)
#define TANK_Y(i)       ZPX(0x98, i)
#define TANK_STATE(i)   ZPX(0xA0, i)
#define TANK_TYPE(i)    ZPX(0xA8, i)
#define TANK_B0(i)      ZPX(0xB0, i)
#define TANK_FREEZE(i)  ZPX(0x6F, i)
#define PLAYER_SHIELD(i) ZPX(0x89, i)
#define BULLET_X(i)     ZPX(0xB8, i)
#define BULLET_Y(i)     ZPX(0xC2, i)
#define BULLET_STATE(i) ZPX(0xCC, i)

/* ---- sprite primitives ---------------------------------------------- */

/* $DA2B draw_sprite_oam
 * Append one 8x16 hardware sprite to OAM at oam_index: Y = y-8, tile =
 * sprite_tile, attr = sprite_attr (| behind_trees if the cell under the
 * sprite's pixel (x+3, y) is the tree tile $22), X = x. Then
 * oam_index += oam_step. Side effects: cell_x = x, cell_y = y-8, ptr ($11)
 * and scratch_00 from the map lookup.
 * QUIRK: oam_index is not wrapped: at index >= $FD the writes spill into
 * $0300+ (sound_req) exactly like abs,X in the original. */
void draw_sprite_oam(uint8_t x, uint8_t y) {
    cell_x = x;
    cell_y = (uint8_t)(y - 8);
    coords_to_map((uint8_t)(x + 3), y);   /* QUIRK: looks up the unshifted y */
    if (ind_y(0x11, 0) == 0x22) sprite_attr = (uint8_t)(sprite_attr | behind_trees);
    uint8_t i = oam_index;
    RAM[0x200 + i]     = cell_y;
    RAM[0x200 + i + 1] = sprite_tile;
    RAM[0x200 + i + 2] = sprite_attr;
    RAM[0x200 + i + 3] = cell_x;
    oam_index = (uint8_t)(i + oam_step);
}
void sub_DA2B(void) { draw_sprite_oam(X, Y); }

/* $DA7B draw_sprite16_at
 * One 16x16 object: left half (tile T) at x-8, right half (tile T+2) at x,
 * both at y. sprite_tile ends as T+2.
 * scratch_54/scratch_55 keep x/y. */
void draw_sprite16_at(uint8_t x, uint8_t y) {
    RAM[0x54] = x;
    RAM[0x55] = y;
    draw_sprite_oam((uint8_t)(x - 8), y);
    sprite_tile = (uint8_t)(sprite_tile + 2);
    draw_sprite_oam(RAM[0x54], RAM[0x55]);
}
void sub_DA7B(void) { draw_sprite16_at(X, Y); }

/* $DA64 draw_sprite: single 8x16 sprite, tile += facing*2, drawn at x-5
 * (used for the bullet). */
void draw_sprite(uint8_t facing, uint8_t x, uint8_t y) {
    sprite_tile = (uint8_t)(sprite_tile + (uint8_t)(facing << 1));
    draw_sprite_oam((uint8_t)(x - 5), y);
}
void sub_DA64(void) { draw_sprite(A, X, Y); }

/* $DA73 draw_sprite16: 16x16 object whose tile is advanced by facing*8
 * (four facings x 8 tiles), then drawn as above. */
void draw_sprite16(uint8_t facing, uint8_t x, uint8_t y) {
    sprite_tile = (uint8_t)(sprite_tile + (uint8_t)(facing << 3));
    draw_sprite16_at(x, y);
}
void sub_DA73(void) { draw_sprite16(A, X, Y); }

/* ---- explosions ------------------------------------------------------ */

/* $DEF0 draw_explosion_frame: explosion object with tile $F1+frame, palette 3.
 * frame is 0 / 4 / 8 -> tiles $F1 (small), $F5 (medium), $F9 (large). */
void draw_explosion_frame(uint8_t frame, uint8_t x, uint8_t y) {
    sprite_tile = (uint8_t)(frame + 0xF1);
    sprite_attr = 3;
    draw_sprite16_at(x, y);
}
void sub_DEF0(void) { draw_explosion_frame(A, X, Y); }

/* $DEE2 draw_explosion_sprite: state's high nibble n (5..7) -> frame
 * (7-n)*4, i.e. the explosion grows as the state counts down. */
void draw_explosion_sprite(uint8_t state, uint8_t x, uint8_t y) {
    uint8_t n = (uint8_t)(state >> 4);
    uint8_t frame = (uint8_t)((uint8_t)(-(uint8_t)(n - 7)) << 2);
    draw_explosion_frame(frame, x, y);
}
void sub_DEE2(void) { draw_explosion_sprite(A, X, Y); }

/* $DECD draw_explosion (tank state $5x-$7x): growing explosion at the tank.
 * behind_trees is forced 0 (explosions are always in front) and restored to
 * $20 afterwards. */
void draw_explosion(uint8_t slot) {
    behind_trees = 0;
    draw_explosion_sprite(TANK_STATE(slot), TANK_X(slot), TANK_Y(slot));
    behind_trees = 0x20;
}
void sub_DECD(void) { draw_explosion(X); }

/* $DEFD draw_death_tail (tank state $1x): for an enemy (tank_type != 0) the
 * floating score popup, tile $B9 + 4*kind (kind = (type_hi>>1)-4, 0..3 =
 * 100/200/300/400); for a player (type 0) the small explosion frame. */
void draw_death_tail(uint8_t slot) {
    behind_trees = 0;
    uint8_t type = TANK_TYPE(slot);
    if (type != 0) {
        sprite_tile = (uint8_t)((uint8_t)(((type >> 3) & 0xFC) - 0x10) + 0xB9);
        sprite_attr = 3;
        draw_sprite16_at(TANK_X(slot), TANK_Y(slot));
    } else {
        draw_explosion_frame(0, TANK_X(slot), TANK_Y(slot));
    }
    behind_trees = 0x20;
}
void sub_DEFD(void) { draw_death_tail(X); }

/* $DF33 draw_explosion_last (tank state $2x): the large puff, tile $F9 */
void draw_explosion_last(uint8_t slot) {
    behind_trees = 0;
    draw_explosion_frame(8, TANK_X(slot), TANK_Y(slot));
    behind_trees = 0x20;
}
void sub_DF33(void) { draw_explosion_last(X); }

/* $DF99 big_explosion_tile: set sprite_tile for quadrant q of the 32x32
 * explosion: tile = (((state&$F0)-$30) ^ $10) + $D1 + q*4. Leaves the tile
 * base in scratch_00 and returns the tank's x (and y in *y).
 * QUIRK: the slot is taken from slot_idx ($5A), not from the caller's X, so
 * draw_explosion_big only works when called from a loop that keeps slot_idx
 * equal to the slot (draw_all_tanks does). */
uint8_t big_explosion_tile(uint8_t q, uint8_t *y) {
    uint8_t slot = slot_idx;
    scratch_00 = (uint8_t)((uint8_t)(q << 2) + 0xD1);
    uint8_t t = (uint8_t)(((TANK_STATE(slot) & 0xF0) - 0x30) ^ 0x10);
    sprite_tile = (uint8_t)(t + scratch_00);
    *y = TANK_Y(slot);
    return TANK_X(slot);
}
void sub_DF99(void) { uint8_t y; X = big_explosion_tile(A, &y); Y = y; }

/* $DF46 draw_explosion_big (tank state $3x/$4x): 32x32 explosion from four
 * 16x16 objects (8 hardware sprites) centred on the tank, quadrants at
 * (-8,-8) (+8,-8) (-8,+8) (+8,+8); palette 3. Tiles $D1.. / $E1.. by state. */
void draw_explosion_big(void) {
    uint8_t x, y;
    sprite_attr = 3;
    behind_trees = 0;
    x = big_explosion_tile(0, &y);
    draw_sprite16_at((uint8_t)(x - 8), (uint8_t)(y - 8));
    x = big_explosion_tile(1, &y);
    draw_sprite16_at((uint8_t)(x + 8), (uint8_t)(y - 8));
    x = big_explosion_tile(2, &y);
    draw_sprite16_at((uint8_t)(x - 8), (uint8_t)(y + 8));
    x = big_explosion_tile(3, &y);
    draw_sprite16_at((uint8_t)(x + 8), (uint8_t)(y + 8));
    behind_trees = 0x20;
}
void sub_DF46(void) { draw_explosion_big(); }

/* ---- tanks ----------------------------------------------------------- */

/* $DFB6 draw_tank_body (tank state $8x-$Dx): live tank, 16x16.
 *  Palette: players (slot 0/1) = slot number (p1 0, p2 1); a stunned player
 *  (tank_freeze != 0) is skipped on frames with frame_counter bit 3 set
 *  (blink; scratch_04 is not written then). Enemies (slot >= 2): the
 *  flashing/bonus tank (tank_type bit 2) alternates palette 2/3 every 8
 *  frames; others use tbl_enemy_palette[(frame*4 + type) & 7] ($E003:
 *  02 00 00 01 02 01 02 02), which flickers between two palettes by type.
 *  Tile = (tank_type & $F0) + tank_tread + facing*8: type high nibble picks the
 *  tank design set (also the armour/level), tank_tread (0/4) the tread frame. */
void draw_tank_body(uint8_t slot) {
    uint8_t pal;
    if (slot >= 2) {
        if (TANK_TYPE(slot) & 0x04) {
            pal = (uint8_t)(((frame_counter >> 3) & 1) + 2);
        } else {
            uint8_t idx = (uint8_t)(((uint8_t)(frame_counter << 2) + TANK_TYPE(slot)) & 7);
            pal = ROM8(0xE003 + idx);
        }
    } else {
        if (TANK_FREEZE(slot) != 0 && (frame_counter & 0x08)) return;
        pal = slot;
    }
    sprite_attr = pal;
    uint8_t facing = TANK_STATE(slot) & 3;
    sprite_tile = (uint8_t)((TANK_TYPE(slot) & 0xF0) + TANK_B0(slot));
    draw_sprite16(facing, TANK_X(slot), TANK_Y(slot));
}
void sub_DFB6(void) { draw_tank_body(X); }

/* $E00B draw_spawn_twinkle (tank state $Ex/$Fx): the 4-frame star that
 * shrinks and grows around the spawn: n = |(state&$F) - 7|, tile =
 * $A1 + ((n*2) & $FC) -> $A1 $A5 $A9 $AD; palette 3. */
void draw_spawn_twinkle(uint8_t slot) {
    uint8_t n = (uint8_t)((TANK_STATE(slot) & 0x0F) - 7);
    if (n & 0x80) n = (uint8_t)(-n);
    sprite_tile = (uint8_t)((uint8_t)((n << 1) & 0xFC) + 0xA1);
    sprite_attr = 3;
    draw_sprite16_at(TANK_X(slot), TANK_Y(slot));
}
void sub_E00B(void) { draw_spawn_twinkle(X); }

/* $DEB8 draw_tank: draw one tank slot, dispatching on the state's high nibble
 * through the table at $E4B8 (16 words; the pointer is left in ptr $11):
 *   0x: nothing (rts at $DBF0)   1x: death tail     2x: last explosion puff
 *   3x,4x: big 32x32 explosion   5x-7x: explosion   8x-Dx: live tank
 *   Ex,Fx: spawn twinkle */
void draw_tank(uint8_t slot) {
    uint8_t y = (uint8_t)(((TANK_STATE(slot) >> 3) & 0xFE));
    uint16_t target = (uint16_t)(ROM8(0xE4B8 + y) | (ROM8(0xE4B9 + y) << 8));
    ptr_lo = (uint8_t)target;
    ptr_hi = (uint8_t)(target >> 8);
    switch (target) {
    case 0xDEFD: draw_death_tail(slot); break;
    case 0xDF33: draw_explosion_last(slot); break;
    case 0xDF46: draw_explosion_big(); break;
    case 0xDECD: draw_explosion(slot); break;
    case 0xDFB6: draw_tank_body(slot); break;
    case 0xE00B: draw_spawn_twinkle(slot); break;
    default: break;   /* $DBF0 = rts */
    }
}
void sub_DEB8(void) { draw_tank(X); }

/* $DEA6 draw_all_tanks: draw tank slots 0..7 in order. slot_idx is the loop
 * counter (and is read by big_explosion_tile); it is 8 afterwards. */
void draw_all_tanks(void) {
    for (slot_idx = 0; slot_idx != 8; slot_idx++) draw_tank(slot_idx);
}
void sub_DEA6(void) { draw_all_tanks(); }

/* ---- bullets ---------------------------------------------------------- */

/* $E0FB draw_bullet_sprite (bullet state $4x, in flight): one 8x16 sprite,
 * tile $B1 + facing*2, palette 2, drawn at x-5. */
void draw_bullet_sprite(uint8_t slot) {
    uint8_t facing = BULLET_STATE(slot) & 3;
    uint8_t y = BULLET_Y(slot);
    uint8_t x = BULLET_X(slot);   /* both read before sprite_attr is stored (slot may alias $04) */
    sprite_attr = 2;
    sprite_tile = 0xB1;
    draw_sprite(facing, x, y);
}
void sub_E0FB(void) { draw_bullet_sprite(X); }

/* $E112 draw_bullet_explosion (bullet state $1x-$3x, hit animation): the
 * explosion sprite for state+$40 (so $3x = small, $2x = medium, $1x = large
 * frames of the tile set $F1/$F5/$F9). */
void draw_bullet_explosion(uint8_t slot) {
    draw_explosion_sprite((uint8_t)(BULLET_STATE(slot) + 0x40), BULLET_X(slot), BULLET_Y(slot));
}
void sub_E112(void) { draw_bullet_explosion(X); }

/* $E0E6 draw_bullet: dispatch on bullet_state's high nibble through the
 * 5-entry table at $E4E2: 0x nothing, 1x-3x explosion, 4x flying bullet.
 * QUIRK: a high nibble above 4 would index past the table into other data
 * (never happens in play); here such targets are ignored. */
void draw_bullet(uint8_t slot) {
    uint8_t y = (uint8_t)((BULLET_STATE(slot) >> 3) & 0xFE);
    uint16_t target = (uint16_t)(ROM8(0xE4E2 + y) | (ROM8(0xE4E3 + y) << 8));
    ptr_lo = (uint8_t)target;
    ptr_hi = (uint8_t)(target >> 8);
    switch (target) {
    case 0xE112: draw_bullet_explosion(slot); break;
    case 0xE0FB: draw_bullet_sprite(slot); break;
    default: break;   /* $DBF0 = rts */
    }
}
void sub_E0E6(void) { draw_bullet(X); }
/* difftest input domain (tools/harness/difftest.c): bullet_state < $50.
 * Every writer stores $00, $33, $40|dir, a copy of another slot, or a
 * countdown from $3x ($E076-$E0A0, $E148, $E158, $E40D, $E6C1, $E6D0,
 * $E964); larger values would jump into the middle of other routines. */

/* $E0D8 draw_bullets: draw bullet slots 9..0 (slot_idx ends at $FF). */
void draw_bullets(void) {
    for (slot_idx = 9; slot_idx < 0x80; slot_idx--) draw_bullet(slot_idx);
}
void sub_E0D8(void) { draw_bullets(); }

/* ---- powerup icon, shield --------------------------------------------- */

/* $E23B draw_powerup: if a powerup is on the field (powerup_x != 0):
 *  - collected (powerup_flash != 0): count it down; at 0 remove the powerup;
 *    otherwise show the score popup tile $3B (palette 2);
 *  - else blink: visible only while frame_counter bit 3 is set, tile
 *    $81 + powerup_type*4 (palette 2; types: helmet, timer, shovel, star,
 *    grenade, tank). Drawn in front of trees. */
void draw_powerup(void) {
    if (powerup_x == 0) return;
    if (powerup_flash != 0) {
        powerup_flash--;
        if (powerup_flash == 0) { powerup_x = 0; return; }
        sprite_attr = 2;
        sprite_tile = 0x3B;
    } else {
        if (!(frame_counter & 0x08)) return;
        sprite_attr = 2;
        sprite_tile = (uint8_t)((uint8_t)(powerup_type << 2) + 0x81);
    }
    behind_trees = 0;
    draw_sprite16_at(powerup_x, powerup_y);
    behind_trees = 0x20;
}
void sub_E23B(void) { draw_powerup(); }

/* $E27C draw_shield: for players 1 then 0 (slot_idx 1,0; $FF afterwards):
 * if player_shield[p] != 0, tick it down every 64 frames and draw the shield
 * over the tank: tile $29 or $2D (flips every 2 frames), palette 2. */
void draw_shield(void) {
    for (slot_idx = 1; slot_idx < 0x80; slot_idx--) {
        uint8_t p = slot_idx;
        if (PLAYER_SHIELD(p) == 0) continue;
        if ((frame_counter & 0x3F) == 0) PLAYER_SHIELD(p)--;
        sprite_attr = 2;
        uint8_t y = TANK_Y(p);
        uint8_t x = TANK_X(p);
        sprite_tile = (uint8_t)((uint8_t)((frame_counter & 0x02) << 1) + 0x29);
        draw_sprite16_at(x, y);
    }
}
void sub_E27C(void) { draw_shield(); }

/* ---- banners and tally tanks ------------------------------------------ */

/* $C8F9 draw_pause_banner: while paused, on frames with frame_counter bit 4
 * set, five 8x16 sprites (tiles $17 $19 $1B $1D $1F = "PAUSE") at y=$80,
 * x=$64,$6C,$74,$7C,$84; palette 3, in front of trees. */
void draw_pause_banner(void) {
    if (paused == 0) return;
    if (!(frame_counter & 0x10)) return;
    sprite_attr = 3;
    behind_trees = 0;
    for (int i = 0; i < 5; i++) {
        sprite_tile = (uint8_t)(0x17 + 2 * i);
        draw_sprite_oam((uint8_t)(0x64 + 8 * i), 0x80);
    }
    behind_trees = 0x20;
}
void sub_C8F9(void) { draw_pause_banner(); }

/* $C947 draw_game_over_banner: "GAME" ($79) and "OVER" ($7D), two 16x16
 * objects at (game_over_x, game_over_y) and (game_over_x+$10, game_over_y); palette 3. */
void draw_game_over_banner(void) {
    sprite_attr = 3;
    behind_trees = 0;
    sprite_tile = 0x79;
    draw_sprite16_at(game_over_x, game_over_y);
    sprite_tile = 0x7D;
    draw_sprite16_at((uint8_t)(game_over_x + 0x10), game_over_y);
    behind_trees = 0x20;
}
void sub_C947(void) { draw_game_over_banner(); }

/* $C972 game_over_slide: the GAME OVER banner. game_over_timer counts down every
 * 16 frames (parking the banner at y=$F0 when it hits 0); while >= $0A it
 * moves by (tbl_dx3, tbl_dy3)[game_over_dir] ($D3D5/$D3D9) per
 * frame, then it is drawn. Does nothing if game_over_timer is 0 or in the demo
 * (game_mode 2). */
void game_over_slide(void) {
    if (game_over_timer == 0) return;
    if (game_mode == 2) return;
    if ((frame_counter & 0x0F) == 0) {
        game_over_timer--;
        if (game_over_timer == 0) game_over_y = 0xF0;
    }
    if (game_over_timer >= 0x0A) {
        uint8_t dir = game_over_dir;
        game_over_x = (uint8_t)(game_over_x + ROM8(0xD3D5 + dir));
        game_over_y = (uint8_t)(game_over_y + ROM8(0xD3D9 + dir));
    }
    draw_game_over_banner();
}
void sub_C972(void) { game_over_slide(); }

/* $D130 draw_tally_tank: one 16x16 tank (tile = tank type tile, facing up)
 * with right half at x=$81 (so the object spans $79-$89). */
void draw_tally_tank(uint8_t tile, uint8_t y) {
    sprite_tile = tile;
    draw_sprite16_at(0x81, y);
}
void sub_D130(void) { draw_tally_tank(A, Y); }

/* $D0B8 draw_tally_sprites: the four enemy kinds' tanks down the tally
 * screen: tiles $80 $A0 $C0 $E0 at y = $64 $7C $94 $AC; palette 2. */
void draw_tally_sprites(void) {
    sprite_attr = 2;
    draw_tally_tank(0x80, 0x64);
    draw_tally_tank(0xA0, 0x7C);
    draw_tally_tank(0xC0, 0x94);
    draw_tally_tank(0xE0, 0xAC);
}
void sub_D0B8(void) { draw_tally_sprites(); }
