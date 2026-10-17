#include "nes.h"
#include <stdlib.h>
#include <string.h>

/* NTSC: 341 dots * 262 lines / 3 = 29780.67 CPU cycles per frame. */
#define FRAME_CYCLES_X3 (341 * 262)

static void iolog(nes *n, uint16_t addr, uint8_t v, int is_read) {
    if (!n->iolog_enabled) return;
    if (n->iolog_len >= IOLOG_MAX) { n->iolog_overflow = 1; return; }
    io_event *e = &n->iolog[n->iolog_len++];
    e->frame = n->frame; e->addr = addr; e->value = v; e->is_read = (uint8_t)is_read;
}

static uint16_t nt_index_m(int horiz, uint16_t a) {
    /* vertical mirroring: $2000=$2800, $2400=$2C00 (selects by A10)
     * horizontal mirroring: $2000=$2400, $2800=$2C00 (selects by A11) */
    if (horiz) return (a & 0x03FF) | ((a & 0x0800) ? 0x400 : 0);
    return (a & 0x03FF) | ((a & 0x0400) ? 0x400 : 0);
}
#define nt_index(a) nt_index_m(n->horizontal, (a))

static uint8_t ppu_bus_read(nes *n, uint16_t a) {
    a &= 0x3FFF;
    if (a < 0x2000) return n->chr[a & 0x1FFF];
    if (a < 0x3F00) return n->vram[nt_index(a)];
    uint8_t i = a & 0x1F;
    if ((i & 0x13) == 0x10) i &= 0x0F;
    return n->palette[i];
}
static void ppu_bus_write(nes *n, uint16_t a, uint8_t v) {
    a &= 0x3FFF;
    if (a < 0x2000) return; /* CHR ROM */
    if (a < 0x3F00) { n->vram[nt_index(a)] = v; return; }
    uint8_t i = a & 0x1F;
    if ((i & 0x13) == 0x10) i &= 0x0F;
    n->palette[i] = v & 0x3F;
}

static uint8_t ppu_reg_read(nes *n, uint16_t r) {
    uint8_t v = 0;
    switch (r) {
    case 2:
        v = (n->status & 0xE0) | (n->read_buf & 0x1F);
        n->status &= ~0x80;
        n->w = 0;
        break;
    case 4: v = n->oam[n->oam_addr]; break;
    case 7: {
        uint16_t a = n->v & 0x3FFF;
        if (a >= 0x3F00) { v = ppu_bus_read(n, a); n->read_buf = n->vram[nt_index(a)]; }
        else { v = n->read_buf; n->read_buf = ppu_bus_read(n, a); }
        n->v = (n->v + ((n->ctrl & 4) ? 32 : 1)) & 0x7FFF;
        break;
    }
    default: v = n->read_buf; break;
    }
    return v;
}

static void ppu_reg_write(nes *n, uint16_t r, uint8_t v) {
    switch (r) {
    case 0: {
        int was = n->ctrl & 0x80;
        n->ctrl = v;
        n->t = (n->t & 0xF3FF) | ((v & 3) << 10);
        /* enabling NMI while in vblank with flag set fires an NMI */
        if (!was && (v & 0x80) && (n->status & 0x80)) n->nmi_pending = 1;
        break;
    }
    case 1: n->mask = v; break;
    case 3: n->oam_addr = v; break;
    case 4: n->oam[n->oam_addr++] = v; break;
    case 5:
        if (!n->w) { n->t = (n->t & 0xFFE0) | (v >> 3); n->fine_x = v & 7; }
        else { n->t = (n->t & 0x8C1F) | ((v & 7) << 12) | ((v & 0xF8) << 2); }
        n->w ^= 1;
        break;
    case 6:
        if (!n->w) n->t = (n->t & 0x00FF) | ((v & 0x3F) << 8);
        else { n->t = (n->t & 0xFF00) | v; n->v = n->t; }
        n->w ^= 1;
        break;
    case 7:
        ppu_bus_write(n, n->v, v);
        n->v = (n->v + ((n->ctrl & 4) ? 32 : 1)) & 0x7FFF;
        break;
    }
}

static uint8_t bus_read(void *ctx, uint16_t a) {
    nes *n = ctx;
    if (a < 0x2000) {
        uint16_t r = a & 0x7FF;
        if (!n->written[r] && !n->uninit_pc[r]) n->uninit_pc[r] = n->cur_ins_pc ? n->cur_ins_pc : 1;
        return n->ram[r];
    }
    if (a < 0x4000) {
        uint8_t v = ppu_reg_read(n, a & 7);
        iolog(n, 0x2000 | (a & 7), v, 1);
        return v;
    }
    if (a >= 0x8000) {
        uint16_t o = a & 0x3FFF;
        if (n->cov_enabled) n->cov[o] |= n->cpu.exec_phase ? 4 : (a != n->cur_ins_pc ? 2 : 0);
        return n->prg[o];
    }
    if (a == 0x4016 || a == 0x4017) {
        int p = a & 1;
        uint8_t v;
        if (n->strobe) v = n->pad[p] & 1;
        else { v = n->pad_shift[p] & 1; n->pad_shift[p] = (n->pad_shift[p] >> 1) | 0x80; }
        v |= 0x40; /* open bus upper bits */
        iolog(n, a, v, 1);
        return v;
    }
    if (a == 0x4015) { iolog(n, a, 0, 1); return 0; }
    return 0;
}

static void bus_write(void *ctx, uint16_t a, uint8_t v) {
    nes *n = ctx;
    if (a < 0x2000) { n->ram[a & 0x7FF] = v; n->written[a & 0x7FF] = 1; return; }
    if (a < 0x4000) { iolog(n, 0x2000 | (a & 7), v, 0); ppu_reg_write(n, a & 7, v); return; }
    if (a == 0x4014) {
        iolog(n, a, v, 0);
        for (int i = 0; i < 256; i++)
            n->oam[(n->oam_addr + i) & 0xFF] = bus_read(n, (uint16_t)((v << 8) | i));
        n->cpu.cycles += 513 + (n->cpu.cycles & 1);
        return;
    }
    if (a == 0x4016) {
        iolog(n, a, v, 0);
        n->strobe = v & 1;
        if (n->strobe) { n->pad_shift[0] = n->pad[0]; n->pad_shift[1] = n->pad[1]; }
        return;
    }
    if (a >= 0x4000 && a <= 0x4017) {
        iolog(n, a, v, 0);
        n->apu[a - 0x4000] = v;
        if (n->apu_hook) n->apu_hook(n, a, v);
        return;
    }
}

uint8_t nes_io_read(nes *n, uint16_t a) { return bus_read(n, a); }
void nes_io_write(nes *n, uint16_t a, uint8_t v) { bus_write(n, a, v); }
/* vblank start / pre-render bookkeeping for drivers without a CPU */
void nes_vblank_start(nes *n) { n->status |= 0x80; }
void nes_prerender(nes *n) {
    n->status &= ~0xE0;
    n->frame_ctrl = n->ctrl;
    n->frame_mask = n->mask;
    n->frame_t = n->t;
    n->frame_fine_x = n->fine_x;
    memcpy(n->frame_oam, n->oam, 256);
}

static void on_exec(void *ctx, uint16_t pc) {
    nes *n = ctx;
    n->cur_ins_pc = pc;
    if (n->exec_hook) n->exec_hook(n, pc);
    if (n->cpu.sp < n->min_sp && n->frame > 0) { n->min_sp = n->cpu.sp; n->min_sp_pc = pc; }
    if (n->cov_enabled && pc >= 0x8000) n->cov[pc & 0x3FFF] |= 1;
}

nes *nes_new(const uint8_t *prg, const uint8_t *chr) {
    nes *n = calloc(1, sizeof *n);
    memcpy(n->prg, prg, 0x4000);
    memcpy(n->chr, chr, 0x2000);
    n->cpu.ctx = n;
    n->cpu.read = bus_read;
    n->cpu.write = bus_write;
    n->cpu.on_exec = on_exec;
    n->iolog_enabled = 1;
    return n;
}
void nes_free(nes *n) { free(n); }

void nes_power(nes *n) {
    for (int i = 0; i < 0x800; i++)
        n->ram[i] = n->ram_init == 2 ? 0 : ((i & 4) ? 0xFF : 0x00);
    memset(n->written, 0, sizeof n->written);
    memset(n->uninit_pc, 0, sizeof n->uninit_pc);
    memset(n->oam, 0, sizeof n->oam);
    memset(n->vram, 0, sizeof n->vram);
    memset(n->palette, 0, sizeof n->palette);
    n->ctrl = n->mask = n->status = n->oam_addr = 0;
    n->v = n->t = 0; n->fine_x = n->w = n->read_buf = 0;
    n->frame = 0; n->nmi_pending = 0;
    n->cpu.cycles = 0;
    n->min_sp = 0xFF;
    cpu_reset(&n->cpu);
    /* Power-on happens mid-frame; we start counting at the pre-render line,
     * i.e. the CPU runs a full visible frame before the first vblank. */
    n->cycle_x3 = 0;
    n->carry_x3 = 0;
    n->ppu_dead = 2;   /* FCEUX: ppudead = 2 at power-on (two frames without vblank) */
}

static void run_until(nes *n, long target_x3) {
    while (n->cycle_x3 < target_x3 && !n->cpu.halted) {
        if (n->nmi_pending) {
            n->nmi_pending = 0;
            uint64_t c0 = n->cpu.cycles;
            n->nmi_pc_last = n->cpu.pc;
            n->nmi_count++;
            n->nmi_snap_valid = 1;
            memcpy(n->nmi_ram, n->ram, 0x800);
            n->nmi_sp = n->cpu.sp;
            n->nmi_iolog_pos = n->iolog_len;
            cpu_nmi(&n->cpu);
            n->cycle_x3 += 3 * (long)(n->cpu.cycles - c0);
            continue;
        }
        uint64_t c0 = n->cpu.cycles;
        cpu_step(&n->cpu);
        n->cycle_x3 += 3 * (long)(n->cpu.cycles - c0);
    }
}

/* One frame, aligned with FCEUX's frame loop so per-frame samples line up:
 *   line 240 (post-render), line 241 vblank set + NMI, lines 242-260,
 *   line 261 pre-render (flags cleared, render state latched),
 *   lines 0-239 visible. The frame returns at the end of line 239.
 * The first frame after power-on has no vblank (PPU warm-up), as in FCEUX. */
void nes_run_frame(nes *n, uint8_t pad1, uint8_t pad2) {
    n->pad[0] = pad1; n->pad[1] = pad2;
    n->cycle_x3 = n->carry_x3;
    if (n->ppu_dead) {
        n->ppu_dead--;
        run_until(n, FRAME_CYCLES_X3);
    } else {
        run_until(n, 341);
        n->status |= 0x80;
        if (n->ctrl & 0x80) n->nmi_pending = 1;
        run_until(n, 341 * 21);
        nes_prerender(n);
        run_until(n, FRAME_CYCLES_X3);
    }
    n->carry_x3 = n->cycle_x3 - FRAME_CYCLES_X3; /* instruction overrun */
    n->frame++;
}

uint8_t nes_peek(nes *n, uint16_t a) {
    if (a < 0x2000) return n->ram[a & 0x7FF];
    if (a >= 0x8000) return n->prg[a & 0x3FFF];
    return 0;
}
void nes_poke(nes *n, uint16_t a, uint8_t v) {
    if (a < 0x2000) n->ram[a & 0x7FF] = v;
}

int nes_call(nes *n, uint16_t addr, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *p, long max_steps) {
    cpu6502 *c = &n->cpu;
    c->halted = 0;
    c->a = *a; c->x = *x; c->y = *y; c->p = *p | FU;
    uint8_t sp0 = c->sp;
    /* push return address 0xFFFF so RTS lands on 0x0000 */
    c->write(n, 0x100 | c->sp, 0xFF); c->sp--;
    c->write(n, 0x100 | c->sp, 0xFF); c->sp--;
    c->pc = addr;
    long steps = 0;
    int rc = 0;
    for (;;) {
        if (c->pc == 0x0000 && c->sp == sp0) break;
        if (steps++ >= max_steps) { rc = 1; break; }
        cpu_step(c);
        if (c->halted) { rc = 2; break; }
    }
    *a = c->a; *x = c->x; *y = c->y; *p = c->p;
    return rc;
}

/* ---- rendering (whole frame from latched state) ---- */
static const uint8_t pal_rgb[64][3] = {
    {84,84,84},{0,30,116},{8,16,144},{48,0,136},{68,0,100},{92,0,48},{84,4,0},{60,24,0},
    {32,42,0},{8,58,0},{0,64,0},{0,60,0},{0,50,60},{0,0,0},{0,0,0},{0,0,0},
    {152,150,152},{8,76,196},{48,50,236},{92,30,228},{136,20,176},{160,20,100},{152,34,32},{120,60,0},
    {84,90,0},{40,114,0},{8,124,0},{0,118,40},{0,102,120},{0,0,0},{0,0,0},{0,0,0},
    {236,238,236},{76,154,236},{120,124,236},{176,98,236},{228,84,236},{236,88,180},{236,106,100},{212,136,32},
    {160,170,0},{116,196,0},{76,208,32},{56,204,108},{56,180,204},{60,60,60},{0,0,0},{0,0,0},
    {236,238,236},{168,204,236},{188,188,236},{212,178,236},{236,174,236},{236,174,212},{236,180,176},{228,196,144},
    {204,210,120},{180,222,120},{168,226,144},{152,226,180},{160,214,228},{160,162,160},{0,0,0},{0,0,0},
};

void nes_render(nes *n, uint8_t *rgb) {
    uint8_t bgidx[256 * 240];
    uint16_t t = n->frame_t;
    int sx = ((t & 0x1F) << 3) | n->frame_fine_x;
    int sy = (((t >> 5) & 0x1F) << 3) | ((t >> 12) & 7);
    int nt = (t >> 10) & 3;
    int base_x = sx + ((nt & 1) ? 256 : 0);
    int base_y = sy + ((nt & 2) ? 240 : 0);
    uint16_t bg_pat = (n->frame_ctrl & 0x10) ? 0x1000 : 0;
    int show_bg = n->frame_mask & 0x08, show_sp = n->frame_mask & 0x10;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 256; x++) {
            uint8_t ci = 0;
            if (show_bg && (x >= 8 || (n->frame_mask & 2))) {
                int wx = (base_x + x) % 512, wy = (base_y + y) % 480;
                int ntn = (wx >= 256 ? 1 : 0) | (wy >= 240 ? 2 : 0);
                int lx = wx & 255, ly = wy % 240;
                uint16_t nta = 0x2000 | (ntn << 10);
                uint8_t tile = ppu_bus_read(n, nta + (ly >> 3) * 32 + (lx >> 3));
                uint8_t at = ppu_bus_read(n, nta + 0x3C0 + (ly >> 5) * 8 + (lx >> 5));
                int sh = ((ly >> 4) & 1) * 4 + ((lx >> 4) & 1) * 2;
                uint8_t palno = (at >> sh) & 3;
                uint16_t pa = bg_pat + tile * 16 + (ly & 7);
                int b = 7 - (lx & 7);
                uint8_t px = ((n->chr[pa] >> b) & 1) | (((n->chr[pa + 8] >> b) & 1) << 1);
                if (px) ci = palno * 4 + px;
            }
            bgidx[y * 256 + x] = ci;
        }
    uint8_t out[256 * 240];
    for (int i = 0; i < 256 * 240; i++) out[i] = ppu_bus_read(n, 0x3F00 + bgidx[i]);
    if (show_sp) {
        int tall = n->frame_ctrl & 0x20;
        uint16_t sp_pat = (n->frame_ctrl & 0x08) ? 0x1000 : 0;
        for (int s = 63; s >= 0; s--) {
            const uint8_t *o = &n->frame_oam[s * 4];
            int sy0 = o[0] + 1, tile = o[1], attr = o[2], sx0 = o[3];
            int h = tall ? 16 : 8;
            for (int r = 0; r < h; r++) {
                int y = sy0 + r;
                if (y >= 240) break;
                int row = (attr & 0x80) ? h - 1 - r : r;
                uint16_t pa;
                if (tall) pa = ((tile & 1) ? 0x1000 : 0) + ((tile & 0xFE) + (row >> 3)) * 16 + (row & 7);
                else pa = sp_pat + tile * 16 + row;
                for (int cx = 0; cx < 8; cx++) {
                    int x = sx0 + cx;
                    if (x >= 256) break;
                    if (x < 8 && !(n->frame_mask & 4)) continue;
                    int b = (attr & 0x40) ? cx : 7 - cx;
                    uint8_t px = ((n->chr[pa] >> b) & 1) | (((n->chr[pa + 8] >> b) & 1) << 1);
                    if (!px) continue;
                    if ((attr & 0x20) && bgidx[y * 256 + x]) continue;
                    out[y * 256 + x] = ppu_bus_read(n, 0x3F10 + (attr & 3) * 4 + px);
                }
            }
        }
    }
    for (int i = 0; i < 256 * 240; i++) {
        const uint8_t *c = pal_rgb[out[i] & 0x3F];
        rgb[i * 3] = c[0]; rgb[i * 3 + 1] = c[1]; rgb[i * 3 + 2] = c[2];
    }
}

void nes_save_state(nes *n, nes_state *s) {
    memcpy(s->ram, n->ram, 0x800);
    s->a = n->cpu.a; s->x = n->cpu.x; s->y = n->cpu.y; s->sp = n->cpu.sp; s->p = n->cpu.p;
    s->ctrl = n->ctrl; s->mask = n->mask; s->status = n->status; s->oam_addr = n->oam_addr;
    s->fine_x = n->fine_x; s->w = n->w; s->read_buf = n->read_buf; s->strobe = n->strobe;
    s->v = n->v; s->t = n->t;
    memcpy(s->oam, n->oam, 256); memcpy(s->vram, n->vram, 0x800); memcpy(s->palette, n->palette, 32);
    memcpy(s->pad, n->pad, 2); memcpy(s->pad_shift, n->pad_shift, 2);
}
void nes_load_state(nes *n, const nes_state *s) {
    memcpy(n->ram, s->ram, 0x800);
    n->cpu.a = s->a; n->cpu.x = s->x; n->cpu.y = s->y; n->cpu.sp = s->sp; n->cpu.p = s->p;
    n->ctrl = s->ctrl; n->mask = s->mask; n->status = s->status; n->oam_addr = s->oam_addr;
    n->fine_x = s->fine_x; n->w = s->w; n->read_buf = s->read_buf; n->strobe = s->strobe;
    n->v = s->v; n->t = s->t;
    memcpy(n->oam, s->oam, 256); memcpy(n->vram, s->vram, 0x800); memcpy(n->palette, s->palette, 32);
    memcpy(n->pad, s->pad, 2); memcpy(n->pad_shift, s->pad_shift, 2);
}
