/* NMI-side PPU work, palettes, playfield rendering, brick text, score digits
 * ($D0D9-$DA93). See docs/subsystems/gfx_text.md for the data formats.
 *
 * Score digit buffers (`digits` at $15, 8 bytes each = 7 digits + $FF):
 *   buf0 $15 player 1 score      buf1 $1D player 2 score
 *   buf2 $25, buf3 $2D           tally-screen point totals for p1 / p2 (add_digits X=2,3 at $CD36/$CD54)
 *   buf4 $35 scratch/popup number (set_score_popup, number_to_digits)
 *   buf5 $3D hi-score
 * Digits are VALUES 0-9, most significant first; draw adds digit_tile_base. */
#include "common.h"

/* from cells.c */
uint8_t cell_to_addr(uint8_t col, uint8_t row, uint8_t *lo);
void map_ptr_from_cell(uint8_t col, uint8_t row);
void locate_pixel(uint8_t px, uint8_t py);
void write_cell(uint8_t v, uint8_t x, uint8_t y);
void clear_quarter_quiet(uint8_t y);
void set_quarter_quiet(uint8_t y);
void write_cell_direct(uint8_t y);
void ptr_advance(uint8_t n);
void queue_block_palette(uint8_t col, uint8_t row);


#define PPUADDR_REG 0x2006
#define PPUDATA_REG 0x2007

/* $D0D9 set_tally_attributes
 * Fills attribute shadow bytes $07C0-$07F7 (the tally screen's palette
 * layout): $50 for 10 bytes, $A0 for 4, $0A for 6, $05 for 6. Queue not used
 * (the screen is drawn with rendering off). */
void set_tally_attributes(void) {
    static const uint8_t p50[] = {0, 1, 2, 3, 8, 9, 10, 13, 14, 15};
    static const uint8_t pA0[] = {4, 5, 6, 7};
    static const uint8_t p0A[] = {16, 17, 18, 21, 22, 23};
    static const uint8_t p05[] = {48, 49, 50, 53, 54, 55};
    for (int i = 0; i < 10; i++) RAM[0x07C0 + p50[i]] = 0x50;
    for (int i = 0; i < 4; i++)  RAM[0x07C0 + pA0[i]] = 0xA0;
    for (int i = 0; i < 6; i++)  RAM[0x07C0 + p0A[i]] = 0x0A;
    for (int i = 0; i < 6; i++)  RAM[0x07C0 + p05[i]] = 0x05;
}
void sub_D0D9(void) { set_tally_attributes(); A = 0x05; SZN(A); }

/* $D138 check_extra_life
 * While the eagle is intact (base_state == $80): award one extra life (and
 * play sound $04/$05 requests) the first time a player's score reaches
 * 20,000 (digit 2 of the 7-digit score >= 2). Player 2 only counts when
 * menu_sel != 0 (two-player game). QUIRK: if player 1 is awarded a life this
 * frame, player 2 is not checked until the next call. */
void check_extra_life(void) {
    if (base_state != 0x80) return;
    int award = 0;
    if (extra_life_given[0] == 0 && digits[2] >= 2) {
        player_lives[0]++;
        extra_life_given[0]++;
        award = 1;
    } else if (menu_sel != 0 && extra_life_given[1] == 0 && digits[10] >= 2) {
        player_lives[1]++;
        extra_life_given[1]++;
        award = 1;
    }
    if (award) { sound_req[4] = 1; sound_req[5] = 1; }
}
void sub_D138(void) { check_extra_life(); }

/* $D47E clear_terrain_map: zero the whole nametable shadow ($0400-$07FF) */
void clear_terrain_map(void) {
    for (int i = 0; i < 0x400; i++) terrain_map[i] = 0;
}
void sub_D47E(void) { clear_terrain_map(); A = 0; X = 0; SZN(0); }

/* $D4E3 write_boot_signature: copy the 16 bytes at $C040 (copyright text)
 * to $0110. Survives a soft reset, so it marks a warm boot. */
void write_boot_signature(void) {
    for (int i = 15; i >= 0; i--) boot_signature[i] = ROM8(0xC040 + i);
}
void sub_D4E3(void) { write_boot_signature(); A = ROM8(0xC040); X = 0xFF; fN = 1; fZ = 0; }

/* $D4EF boot_signature_valid: 1 if $0110..$011F still equals $C040.. ,
 * else 0. Original compares from the end (index 15 down). */
uint8_t boot_signature_valid(uint8_t *xout, uint8_t *cout) {
    int8_t i = 15;
    uint8_t c;
    for (;;) {
        uint8_t r = boot_signature[i], m = ROM8(0xC040 + i);
        c = (r >= m);
        if (r != m) { *xout = (uint8_t)i; *cout = c; return 0; }
        if (--i < 0) { *xout = 0xFF; *cout = c; return 1; }
    }
}
void sub_D4EF(void) {
    uint8_t x, c;
    A = boot_signature_valid(&x, &c);
    X = x; fC = c; SZN(A);
}

/* $D50E load_bg_palette: upload 16-byte background palette row `row` of
 * TBL_BG_PALETTES ($D565) to PPU $3F00, then set palette_request = $FF
 * (nothing pending) and reset the PPU address latch/address to $3F00. */
uint8_t load_bg_palette(uint8_t row) {
    uint8_t x = (uint8_t)(row << 4);
    hal_write(PPUADDR_REG, 0x3F);
    hal_write(PPUADDR_REG, 0x00);
    for (int n = 0; n < 16; n++) {
        hal_write(PPUDATA_REG, ROM8(TBL_BG_PALETTES + x));
        x++;
    }
    palette_request = 0xFF;
    hal_write(PPUADDR_REG, 0x3F);
    hal_write(PPUADDR_REG, 0x00);
    hal_write(PPUADDR_REG, 0x00);
    hal_write(PPUADDR_REG, 0x00);
    return x;
}
void sub_D50E(void) {
    fC = (A >> 4) & 1;
    X = load_bg_palette(A);
    A = 0; Y = 0; SZN(A);
}

/* $D53E load_sprite_palettes: upload the 16 bytes at $D555 to PPU $3F10 */
void load_sprite_palettes(void) {
    hal_write(PPUADDR_REG, 0x3F);
    hal_write(PPUADDR_REG, 0x10);
    for (int n = 0; n < 16; n++) hal_write(PPUDATA_REG, ROM8(TBL_SPRITE_PALETTE + n));
}
void sub_D53E(void) { load_sprite_palettes(); A = ROM8(TBL_SPRITE_PALETTE + 15); X = 16; Y = 0; SZN(0); }

/* $D7B4 render_playfield: write the entire nametable shadow ($0400-$07FF) to
 * the PPU one byte at a time through write_cell_direct (rendering must be
 * off). Target page = shadow page + ppu_page_offset. */
void render_playfield(void) {
    ptr_lo = 0;
    ptr_hi = 4;
    do {
        write_cell_direct(0);
        ptr_advance(1);
    } while (ptr_hi != 8);
}
void sub_D7B4(void) { render_playfield(); A = 8; Y = 0; fC = 1; SZN(0); }

/* $D7CC reset_terrain_map: fill the shadow with $11 (solid border tile),
 * zero the attribute bytes ($07C0-$07FF), then clear a playfield window to 0:
 * top-left cell (tmp_x,tmp_y), cell_w columns by slot_idx rows. */
void reset_terrain_map(void) {
    for (int i = 0; i < 0x400; i++) terrain_map[i] = 0x11;
    for (int i = 0xC0; i < 0x100; i++) terrain_map[0x300 + i] = 0;
    uint8_t lo;
    uint8_t hi = cell_to_addr(tmp_x, tmp_y, &lo);
    ptr_hi = hi;
    ptr_lo = lo;
    for (;;) {
        uint8_t y = (uint8_t)(cell_w - 1);
        /* QUIRK: loops while y is "positive"; cell_w == 0 or > $81 stores once */
        do { ind_y_store(0x11, y, 0); y--; } while (!(y & 0x80));
        if (--slot_idx == 0) break;
        ptr_advance(0x20);
    }
}
void sub_D7CC(void) {
    reset_terrain_map();
    A = 0; X = tmp_x; Y = 0xFF; fZ = 1; fN = 0;
}

/* $D80B draw_block: draw one 16x16 block of type `type` (0-15) with its
 * top-left at pixel (px,py): set the block's palette (TBL_BLOCK_ATTR[type])
 * through the attribute queue, then write the 2x2 tile pattern
 * TBL_BLOCK_CELLS[type*4..+3] (TL,TR,BL,BR) to the map and the VRAM queue.
 * Block types (palette from TBL_BLOCK_ATTR, cells TL,TR,BL,BR):
 *   0 right-half brick, 1 bottom-half brick, 2 left-half brick,
 *   3 top-half brick, 4 full brick            (tiles $00/$0F, palette 0)
 *   5-8 same four halves in steel ($10 with blank $20), 9 full steel
 *                                              (palette 3)
 *   10 water ($12, palette 1), 11 trees ($22, palette 2),
 *   12 ice ($21, palette 3), 13-15 empty ($00, palette 0)
 * cell_x ends as type*4+4 (write_cell uses it as an X save slot). */
void draw_block(uint8_t type, uint8_t px, uint8_t py) {
    scratch_00 = type;
    uint8_t cx = px >> 3, cy = py >> 3;
    cell_x = cx;
    cell_y = cy;
    scratch_04 = ROM8(TBL_BLOCK_ATTR + type);
    queue_block_palette(cell_x, cell_y);
    map_ptr_from_cell(cell_x & 0xFE, cell_y & 0xFE);
    uint8_t x = (uint8_t)(type << 2);
    write_cell(ROM8(TBL_BLOCK_CELLS + x), (uint8_t)(x + 1), 0); x++;
    ptr_advance(1);
    write_cell(ROM8(TBL_BLOCK_CELLS + x), (uint8_t)(x + 1), 0); x++;
    ptr_advance(0x1F);
    write_cell(ROM8(TBL_BLOCK_CELLS + x), (uint8_t)(x + 1), 0); x++;
    ptr_advance(1);
    write_cell(ROM8(TBL_BLOCK_CELLS + x), (uint8_t)(x + 1), 0); x++;
}
void sub_D80B(void) {
    draw_block(A, X, Y);
    X = cell_x;
    A = 0; /* other regs not consumed by callers (contract out is empty) */
}

/* $D85E draw_glyph_bricks: render ONE character as a big brick glyph.
 * It reads the 8 pattern bytes of character tile `tile` (CHR $1000 + tile*16,
 * background table) back from the PPU, and for each of the 8x8 font pixels
 * sets or clears one brick quarter (4x4 px) in the map, so the glyph covers
 * 32x32 screen pixels with its top-left at (px,py). Map only: no queue.
 * Rendering must be off (PPUADDR/PPUDATA reads).
 * Scratch: tally_count ($5D) = running pixel X, $5E = running pixel Y,
 * $02 = pattern row byte, $03 = bit mask, slot_idx = row counter.
 * QUIRK: the tile*16 address loop uses dex/bmi, so tile > $80 advances 0
 * times (tile $81-$FF read the pattern of tile 0). */
void draw_glyph_bricks(uint8_t tile, uint8_t px, uint8_t py) {
    tally_count = px;
    tile_scratch = (uint8_t)(py + 0x20);
    ptr_lo = 0;
    ptr_hi = 0x10;
    for (uint8_t n = tile;;) {
        n--;
        if (n & 0x80) break;
        ptr_advance(0x10);
    }
    hal_write(PPUADDR_REG, ptr_hi);
    hal_write(PPUADDR_REG, ptr_lo);
    (void)hal_read(PPUDATA_REG);          /* dummy read: buffered PPU reads */
    uint8_t s = S;                         /* the original pushes the bytes on the stack */
    slot_idx = 8;
    do {
        RAM[0x100 + s] = hal_read(PPUDATA_REG); s--;
        slot_idx--;
    } while (slot_idx != 0);
    slot_idx = 8;
    do {
        s++;
        RAM[2] = RAM[0x100 + s];           /* last row read comes first (bottom up) */
        RAM[3] = 0x80;
        for (;;) {
            locate_pixel(tally_count, tile_scratch);
            if (RAM[2] & RAM[3]) set_quarter_quiet(0);
            else clear_quarter_quiet(0);
            tally_count = (uint8_t)(tally_count + 4);
            uint8_t carry = RAM[3] & 1;
            RAM[3] >>= 1;
            if (carry) break;
        }
        tally_count = (uint8_t)(tally_count - 0x20);
        tile_scratch = (uint8_t)(tile_scratch - 4);
        slot_idx--;
    } while (slot_idx != 0);
}
void sub_D85E(void) { draw_glyph_bricks(A, X, Y); A = 0; }

/* $D8D2 draw_text_bricks: draw the $FF-terminated string at str_ptr ($13) as
 * brick glyphs, first character at pixel (tmp_x,tmp_y), 32 px apart
 * (tmp_x advances by $20 per character). digit_tile_base is added to each
 * character code. Uses text_index ($5F) to keep the string index. */
void draw_text_bricks(void) {
    uint8_t y = 0;
    text_index = 0;
    for (;;) {
        uint8_t c = ind_y(0x13, y);
        if (c == 0xFF) return;
        y++;
        text_index = y;
        draw_glyph_bricks((uint8_t)(c + digit_tile_base), tmp_x, tmp_y);
        tmp_x = (uint8_t)(tmp_x + 0x20);
        y = text_index;
    }
}
void sub_D8D2(void) { draw_text_bricks(); A = 0xFF; Y = text_index; fC = 1; SZN(A); }

/* $D8FD flush_vram_queue (NMI): send the queue to the PPU.
 * Record: [addr_hi, addr_lo, data..., $FF]. Walks until the read index
 * equals vram_queue_len (a 0 sentinel is written at the end first), then
 * resets vram_queue_len to 0. Returns the final read index (X).
 * QUIRK: a data byte $FF is only a terminator if the NEXT byte is not $FF;
 * "FF FF" writes one literal $FF to the PPU and the second FF then acts
 * as the terminator (a record starting with hi=$FF would never be seen). */
uint8_t flush_vram_queue(void) {
    uint8_t x = vram_queue_len;
    RAM[0x180 + x] = 0;
    x = 0;
    while (x != vram_queue_len) {
        hal_write(PPUADDR_REG, RAM[0x180 + x]); x++;
        hal_write(PPUADDR_REG, RAM[0x180 + x]); x++;
        for (;;) {
            uint8_t a = RAM[0x180 + x]; x++;
            if (a == 0xFF) {
                if (RAM[0x180 + x] != 0xFF) break;     /* terminator: next record */
                a = RAM[0x17F + x];                     /* literal $FF */
            }
            hal_write(PPUDATA_REG, a);
        }
    }
    vram_queue_len = 0;
    return x;
}
void sub_D8FD(void) { X = flush_vram_queue(); A = 0; fC = 1; SZN(0); }

/* $D934 align_number: skip leading zero digits of the number at zero-page
 * offset y (digits are values, buffer ends with $FF), moving the screen
 * column x right with each skipped digit. If the number is all zeros the
 * scan stops on the $FF; then it backs up one digit (to show a single "0")
 * if narrow_zero != 0, or two digits (shows nothing) if narrow_zero == 0.
 * Result: ptr = (0, y) -- start of the digits to draw; returns the new x. */
uint8_t align_number(uint8_t x, uint8_t y) {
    uint8_t a;
    while ((a = RAM[y]) == 0) { y++; x++; }
    if (a == 0xFF) {
        if (narrow_zero == 0) { x--; y--; }
        x--; y--;
    }
    ptr_hi = 0;
    ptr_lo = y;
    return x;
}
void sub_D934(void) { X = align_number(X, Y); Y = ptr_lo; A = 0; SZN(0); }

/* $D951 draw_hiscore_bricks: draw the hi-score (buffer 5, $3D) as brick
 * text at pixel (16,100), leading zeros skipped, using tiles $30+digit.
 * digit_tile_base is restored to 0. */
void draw_hiscore_bricks(void) {
    tmp_x = 0x10;
    tmp_y = 0x64;
    digit_tile_base = 0x30;
    uint8_t y = 0x3D;
    while (RAM[y] == 0) { y++; tmp_x = (uint8_t)(tmp_x + 0x20); }
    str_ptr_hi = 0;
    str_ptr_lo = y;
    draw_text_bricks();
    digit_tile_base = 0;
}
void sub_D951(void) { draw_hiscore_bricks(); A = 0; }

/* $D97D update_hiscore: compare both players' 7-digit scores with the
 * hi-score (digits+40); copy a higher one over it. Returns Y: 0 = no new
 * record, 1 = player 1 set it, $FF = player 2 (also if both, p2 wins when
 * higher than the new p1 record). QUIRK: "higher" is decided by the sign of
 * the 8-bit difference of the first differing digit (fine for 0-9). */
static int score_cmp_hi(const uint8_t *score) {
    /* returns 1 if score > hi (a copy is needed), 0 otherwise */
    for (int i = 0;; ) {
        uint8_t a = score[i], b = digits[40 + i];
        if (a != b) return !(((uint8_t)(a - b)) & 0x80);
        if (++i == 7) return 0;
    }
}
uint8_t update_hiscore(uint8_t *xout) {
    uint8_t y = 0;
    uint8_t x = 0;
    if (score_cmp_hi(digits)) {
        for (int i = 0; i < 7; i++) digits[40 + i] = digits[i];
        y = 1;
    }
    if (score_cmp_hi(digits + 8)) {
        for (int i = 0; i < 7; i++) digits[40 + i] = digits[8 + i];
        y = 0xFF;
    }
    *xout = x;
    return y;
}
void sub_D97D(void) {
    uint8_t x;
    Y = update_hiscore(&x);
    X = x; A = 0; /* X/A not consumed by callers (out = Y only) */
}

/* $D9BE add_digits: add the popup number (buffer 4, $35) into score buffer
 * `buf` (0 = p1, 1 = p2): decimal add of 7 digits, least significant first,
 * carrying 10 -> 1. */
void add_digits(uint8_t buf) {
    uint8_t x = (uint8_t)(buf * 8 + 6);
    uint8_t carry = 0;
    for (int8_t y = 6; y >= 0; y--) {
        uint8_t sum = (uint8_t)(RAM[0x35 + y] + RAM[(uint8_t)(0x15 + x)] + carry);
        if (!((uint8_t)(sum - 10) & 0x80)) { sum -= 10; carry = 1; }
        else carry = 0;
        RAM[(uint8_t)(0x15 + x)] = sum;
        x--;
    }
}
void sub_D9BE(void) { add_digits(X); X = (uint8_t)(X * 8 + 6 - 7); Y = 0xFF; }

/* $D9FE clear_digits: zero 7 zero-page bytes at x.. and set an $FF
 * terminator (zero-page index wraps). */
void clear_digits(uint8_t x) {
    for (int i = 0; i < 7; i++) RAM[(uint8_t)(x + i)] = 0;
    RAM[(uint8_t)(x + 7)] = 0xFF;
}
void sub_D9FE(void) { clear_digits(X); A = 0xFF; SZN(A); }

/* $D9E1 set_score_popup: fill buffer 4 ($35) with the popup number `bcd`
 * (packed BCD, in HUNDREDS): digits 36,37 (offsets from `digits`) get
 * the high/low nibble followed by the always-zero last digit, so $10
 * shows "100". bcd == 0 means 1000 (digits+35 = 1). */
void set_score_popup(uint8_t bcd) {
    scratch_00 = bcd;
    clear_digits(0x35);
    if (bcd == 0) { digits[35] = 1; return; }
    digits[37] = bcd & 0x0F;
    digits[36] = bcd >> 4;
}
void sub_D9E1(void) {
    set_score_popup(A);
    X = 0x35;
}

/* $DA13 number_to_digits: A (0-255) -> decimal tens/units in buffer 4:
 * digits+37 = tens (can exceed 9 above 99), digits+38 = units. Note the
 * 7-digit buffer layout puts them at the last two positions. */
void number_to_digits(uint8_t n) {
    scratch_00 = n;
    clear_digits(0x35);
    uint8_t a = scratch_00;
    while (a >= 10) { a -= 10; digits[37]++; }
    digits[38] = a;
}
void sub_DA13(void) {
    number_to_digits(A);
    X = 0x35; A = digits[38];
}

/* $DA93 clear_oam: hide sprites by writing $F0 into the Y byte of OAM
 * entries from oam_index down to entry 4 (stepping by oam_step), then
 * oam_index = 4. QUIRK: oam_step is negated in place (two's complement),
 * so every call flips its sign. */
void clear_oam(void) {
    uint8_t x = oam_index;
    oam_step = (uint8_t)(-oam_step);
    do {
        x = (uint8_t)(x + oam_step);
        RAM[0x200 + x] = 0xF0;
    } while (x != 4);
    oam_index = x;
}
void sub_DA93(void) { clear_oam(); X = 4; A = 0xF0; fZ = 1; fN = 0; }
