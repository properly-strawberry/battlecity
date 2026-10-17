/* Nametable shadow, terrain cells and the VRAM update queue ($D5FB-$D7B3).
 *
 * Model (verified by these routines):
 *  - terrain_map ($0400-$07FF) is a RAM copy of nametable 0 ($2000-$23FF),
 *    attribute table included ($07C0-$07FF = $23C0-$23FF). A "cell" is one
 *    8x8 background tile; its value is the tile number, and the terrain
 *    logic (brick quarters, concrete, ...) works directly on tile numbers.
 *  - Cell (col,row) lives at $0400 + row*32 + col.
 *  - The PPU is updated through vram_queue ($0180): records of
 *    [addr_hi, addr_lo, data..., $FF], appended at vram_queue_len and
 *    flushed by the NMI ($D8FD).
 *  - ppu_page_offset is the PPU page offset added to the shadow page:
 *    $1C maps $04xx -> $20xx (nametable 0), $24 maps $04xx -> $28xx. */
#include "common.h"

/* append one byte to the VRAM queue; the index wraps at 256 like X does,
 * so an overflowing queue runs into $0200 (OAM buffer) as in the original */
static inline void vq_put(uint8_t *x, uint8_t v) { RAM[0x180 + *x] = v; (*x)++; }

/* $D5FB cell_to_addr
 * Shadow address of cell (col,row): returns high byte (row>>3)|$04 and
 * stores the low byte col|((row&7)<<5) in *lo. Leaves (row&7)<<5 in
 * scratch_00. QUIRK: col is ORed, not added, so col >= 32 aliases. */
uint8_t cell_to_addr(uint8_t col, uint8_t row, uint8_t *lo) {
    scratch_00 = (uint8_t)((row & 7) << 5);
    *lo = col | scratch_00;
    return (uint8_t)((row >> 3) | 0x04);
}
void sub_D5FB(void) { uint8_t lo; A = cell_to_addr(X, Y, &lo); Y = lo; }

/* $D709 point ptr ($11) at cell (col,row). The original returns Y = 0. */
void map_ptr_from_cell(uint8_t col, uint8_t row) {
    uint8_t lo;
    uint8_t hi = cell_to_addr(col, row, &lo);
    ptr_hi = hi;
    ptr_lo = lo;
}
void sub_D709(void) { map_ptr_from_cell(X, Y); Y = 0; }

/* $D713 pixels_to_cells: (x>>3, y>>3) */
void sub_D713(void) { Y >>= 3; X >>= 3; }

/* $D706 coords_to_map: point ptr at the cell under pixel (px,py). The
 * original leaves X = px>>3 and Y = 0. */
void coords_to_map(uint8_t px, uint8_t py) { map_ptr_from_cell(px >> 3, py >> 3); }
void sub_D706(void) { coords_to_map(X, Y); X >>= 3; Y = 0; }

/* $D725 quarter_mask: which quarter of the 8x8 cell a pixel position falls
 * in, as a bit in scratch_00: bit0 top-left, bit1 top-right, bit2
 * bottom-left, bit3 bottom-right (cell_x/cell_y hold pixel coordinates).
 * Brick tiles $00-$0F encode their four 4x4 quarters in these bits. */
void quarter_mask(void) {
    scratch_00 = 1;
    if (cell_y & 4) scratch_00 <<= 2;
    if (cell_x & 4) scratch_00 <<= 1;
}
void sub_D725(void) { quarter_mask(); }

/* $D71E point ptr at the cell under pixel (px,py), remember the pixel in
 * cell_x/cell_y and compute its quarter mask. Original returns Y = 0. */
void locate_pixel(uint8_t px, uint8_t py) {
    cell_x = px;
    cell_y = py;
    coords_to_map(px, py);
    quarter_mask();
}
void sub_D71E(void) { locate_pixel(X, Y); X >>= 3; Y = 0; }

/* $D73C test_quarter: nonzero if the cell at ptr+y has the quarter bit set
 * or is not a plain brick tile (high nibble set). Original returns this in A
 * with Z set when zero. */
uint8_t test_quarter(uint8_t y) { return (uint8_t)((scratch_00 | 0xF0) & ind_y(0x11, y)); }
void sub_D73C(void) { A = test_quarter(Y); SZN(A); }

/* $D784 write_cell: store v at ptr+y and queue the matching nametable write.
 * QUIRK: the queued PPU address is ptr itself (without +y) while the
 * queued data is the byte at ptr+y; every caller passes y = 0.
 * cell_x is used as a save slot for X (clobbered). */
void write_cell(uint8_t v, uint8_t x, uint8_t y) {
    ind_y_store(0x11, y, v);
    cell_x = x;
    uint8_t q = vram_queue_len;
    vq_put(&q, (uint8_t)(ptr_hi + 0x1C));
    vq_put(&q, ptr_lo);
    vq_put(&q, ind_y(0x11, y));
    vq_put(&q, 0xFF);
    vram_queue_len = q;
}
void sub_D784(void) { write_cell(A, X, Y); X = cell_x; }

/* $D743 clear_quarter: clear the quarter bit in the cell at ptr+y and
 * queue the update. */
void clear_quarter(uint8_t x, uint8_t y) {
    write_cell((uint8_t)(~scratch_00 & ind_y(0x11, y)), x, y);
}
void sub_D743(void) { clear_quarter(X, Y); X = cell_x; }

/* $D74D clear the quarter bit, map only (no PPU update), and only if the
 * cell is a brick tile (high nibble 0). */
void clear_quarter_quiet(uint8_t y) {
    uint8_t c = ind_y(0x11, y);
    if ((c & 0xF0) == 0) ind_y_store(0x11, y, (uint8_t)(~scratch_00 & c));
}
void sub_D74D(void) { clear_quarter_quiet(Y); }

/* $D75C-$D763: unreferenced code ("set quarter and queue":
 * lda $00 / ora ($11),y / jsr write_cell / rts). Not reconstructed. */

/* $D764 set the quarter bit, map only, if the cell is a brick tile. */
void set_quarter_quiet(uint8_t y) {
    uint8_t c = ind_y(0x11, y);
    if ((c & 0xF0) == 0) ind_y_store(0x11, y, (uint8_t)(scratch_00 | c));
}
void sub_D764(void) { set_quarter_quiet(Y); }

/* $D771 write_cell_direct: write the cell at ptr+y straight to the PPU
 * (rendering must be off). PPU address = ptr + ppu_page_offset pages. */
void write_cell_direct(uint8_t y) {
    hal_write(0x2006, (uint8_t)(ptr_hi + ppu_page_offset));
    hal_write(0x2006, ptr_lo);
    hal_write(0x2007, ind_y(0x11, y));
}
void sub_D771(void) { write_cell_direct(Y); }

/* $D7AA ptr_advance: ptr += n */
void ptr_advance(uint8_t n) { set_zptr(0x11, (uint16_t)(ZPTR(0x11) + n)); }
void sub_D7AA(void) { ptr_advance(A); }

/* $D684 one step of replicating a 2-bit palette number: (a<<2)|scratch_04 */
uint8_t pal_shift_in(uint8_t a) { return (uint8_t)((a << 2) | scratch_04); }
void sub_D684(void) { A = pal_shift_in(A); }

/* $D635 set the 2-bit palette of the 16x16 block containing cell
 * (col,row) to scratch_04 in the attribute shadow ($07C0). Returns the
 * attribute byte index. Scratch: $00 = (row*2)&$F8, $01 = keep-mask,
 * $02 = new bits. */
uint8_t set_block_palette(uint8_t col, uint8_t row) {
    uint8_t bits = pal_shift_in(pal_shift_in(pal_shift_in(scratch_04)));
    scratch_02 = bits;
    uint8_t keep;
    if (!(row & 2)) keep = (col & 2) ? 0xF3 : 0xFC;   /* top half: TL / TR */
    else keep = (col & 2) ? 0x3F : 0xCF;              /* bottom half: BR / BL */
    scratch_01 = keep;
    scratch_00 = (uint8_t)((row << 1) & 0xF8);
    uint8_t idx = (uint8_t)(scratch_00 + (col >> 2));
    scratch_02 = (uint8_t)(~keep & scratch_02);
    uint16_t a = (uint16_t)(0x07C0 + idx);
    bus_write(a, (uint8_t)((bus_read(a) & keep) | scratch_02));
    return idx;
}
void sub_D635(void) { Y = set_block_palette(X, Y); }

/* $D612 update a block's palette and queue the attribute byte for the PPU
 * ($23C0 + index). */
void queue_block_palette(uint8_t col, uint8_t row) {
    uint8_t idx = set_block_palette(col, row);
    uint8_t q = vram_queue_len;
    vq_put(&q, 0x23);
    vq_put(&q, (uint8_t)(idx + 0xC0));
    vq_put(&q, bus_read((uint16_t)(0x07C0 + idx)));
    vq_put(&q, 0xFF);
    vram_queue_len = q;
}
void sub_D612(void) { queue_block_palette(X, Y); }

/* $D689 read_joypads (NMI): strobe, then read 8 bits from each pad, pad 2
 * ($4017) first. A button counts as pressed if bit 0 or bit 1 is set
 * (bit 1 = Famicom expansion port pads). Updates joy_held and joy_pressed
 * (newly pressed since last frame). Original returns Y = 0. */
void read_joypads(void) {
    hal_write(0x4016, 1);
    hal_write(0x4016, 0);
    for (int p = 1; p >= 0; p--) {
        scratch_00 = 0;
        for (int i = 0; i < 8; i++) {
            uint8_t c = (hal_read((uint16_t)(0x4016 + p)) & 3) != 0;
            scratch_00 = (uint8_t)((scratch_00 >> 1) | (c << 7));
        }
        joy_pressed[p] = (uint8_t)(~joy_held[p] & scratch_00);
        joy_held[p] = scratch_00;
    }
}
void sub_D689(void) { read_joypads(); Y = 0; }

/* $D6B3 draw_tiles: copy an $FF-terminated tile string (at ptr $11) to cell
 * (col,row): into the nametable shadow (via str_ptr $13) and as one queue
 * record. Returns the string length (original: in Y). */
uint8_t draw_tiles(uint8_t col, uint8_t row) {
    uint8_t lo;
    uint8_t hi = cell_to_addr(col, row, &lo);
    str_ptr_hi = hi;
    uint8_t q = vram_queue_len;
    vq_put(&q, (uint8_t)(hi + ppu_page_offset));
    vq_put(&q, lo);
    str_ptr_lo = lo;
    uint8_t y = 0;
    for (;;) {
        uint8_t t = ind_y(0x11, y);
        vq_put(&q, t);
        if (t == 0xFF) break;
        ind_y_store(0x13, y, t);
        y++;
    }
    vram_queue_len = q;
    return y;
}
void sub_D6B3(void) { Y = draw_tiles(X, Y); }

/* $D6DD draw_hud_number: queue an $FF-terminated digit string (ptr $11) at
 * cell (col,row), adding digit_tile_base to every byte below $80. Queue
 * only, the shadow is not touched. */
void draw_hud_number(uint8_t col, uint8_t row) {
    uint8_t lo;
    uint8_t hi = cell_to_addr(col, row, &lo);
    uint8_t q = vram_queue_len;
    vq_put(&q, (uint8_t)(hi + ppu_page_offset));
    vq_put(&q, lo);
    for (uint8_t y = 0;; y++) {
        uint8_t t = ind_y(0x11, y);
        if (!(t & 0x80)) t = (uint8_t)(t + digit_tile_base);
        vq_put(&q, t);
        if (t == 0xFF) break;
    }
    vram_queue_len = q;
}
void sub_D6DD(void) { draw_hud_number(X, Y); }
