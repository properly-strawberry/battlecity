/* Minimal NROM NES harness for the Battle City reference oracle.
 * Scope: CPU-visible behaviour (RAM, PPU/APU register traffic, controller
 * reads, NMI at vblank). The PPU renders a whole frame from end-of-frame
 * state (no mid-frame raster effects). */
#ifndef NES_H
#define NES_H
#include <stdint.h>
#include "cpu6502.h"

#define IOLOG_MAX 65536

typedef struct io_event {
    uint32_t frame;
    uint16_t addr;   /* 0x2000-0x2007, 0x4000-0x4017 */
    uint8_t value;
    uint8_t is_read;
} io_event;

typedef struct nes {
    cpu6502 cpu;
    uint8_t ram[0x800];
    uint8_t prg[0x4000];
    uint8_t chr[0x2000];
    /* PPU */
    uint8_t ctrl, mask, status, oam_addr;
    uint8_t oam[256];
    uint8_t vram[0x800];      /* 2 nametables */
    int horizontal;           /* 1 = horizontal mirroring, 0 = vertical */
    uint8_t palette[32];
    uint16_t v, t; uint8_t fine_x, w, read_buf;
    /* render state latched at the pre-render line of the current frame */
    uint8_t frame_ctrl, frame_mask, frame_fine_x;
    uint16_t frame_t;
    uint8_t frame_oam[256];
    int nmi_pending;
    long cycle_x3;            /* PPU dots since frame start */
    long carry_x3;
    int ppu_dead;
    uint16_t cur_ins_pc;
    void (*exec_hook)(struct nes *n, uint16_t pc);  /* optional, before each instruction */
    uint8_t min_sp;            /* deepest stack pointer seen (after reset's TXS) */
    uint16_t min_sp_pc;
    /* APU (write-only model) */
    uint8_t apu[0x18];
    /* optional, called on every $4000-$4017 write (cpu.cycles = time) */
    void (*apu_hook)(struct nes *n, uint16_t addr, uint8_t v);
    /* controllers */
    uint8_t pad[2], pad_shift[2], strobe;
    /* bookkeeping */
    uint32_t frame;
    /* io log */
    io_event iolog[IOLOG_MAX];
    uint32_t iolog_len;
    int iolog_enabled;
    int iolog_overflow;
    /* coverage: 1 = executed as opcode, 2 = read as operand/data */
    uint8_t cov[0x4000];
    int cov_enabled;
    /* snapshot taken when an NMI is taken (before the handler runs) */
    uint32_t nmi_count;
    int nmi_snap_valid;
    uint8_t nmi_ram[0x800];
    uint8_t nmi_sp;
    uint32_t nmi_iolog_pos;
    /* PC at which the last NMI was taken (0 = none this frame) */
    uint16_t nmi_pc_last;
    /* power-on RAM fill: 0 = FCEUX default (00 00 00 00 FF FF FF FF ...),
     * 2 = all zero */
    int ram_init;
    /* uninitialised-read tracking: written[a] set on first write; on a
     * read of an unwritten byte, uninit_pc[a] records the first reader */
    uint8_t written[0x800];
    uint16_t uninit_pc[0x800];
    /* stop flag for cpu_call */
    uint16_t call_sentinel;
} nes;

/* Compact machine state (everything a routine can observe or change). */
typedef struct nes_state {
    uint8_t ram[0x800];
    uint8_t a, x, y, sp, p;
    uint8_t ctrl, mask, status, oam_addr, fine_x, w, read_buf, strobe;
    uint16_t v, t;
    uint8_t oam[256], vram[0x800], palette[32];
    uint8_t pad[2], pad_shift[2];
} nes_state;
void nes_save_state(nes *n, nes_state *s);
void nes_load_state(nes *n, const nes_state *s);

nes *nes_new(const uint8_t *prg16k, const uint8_t *chr8k);
void nes_free(nes *n);
void nes_power(nes *n);
/* Run one video frame (29781 CPU cycles approx.); see nes.c for the timeline. */
void nes_run_frame(nes *n, uint8_t pad1, uint8_t pad2);
/* Render current PPU state to a 256x240 RGB buffer. */
void nes_render(nes *n, uint8_t *rgb);
/* Call a subroutine at addr with given regs until it returns (RTS to
 * sentinel) or max_steps instructions. Returns 0 ok, 1 timeout, 2 halted. */
int nes_call(nes *n, uint16_t addr, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *p, long max_steps);
uint8_t nes_peek(nes *n, uint16_t addr);
/* CPU-bus access without a CPU (used by the C-reconstruction harness, whose
 * RAM lives in the reconstruction itself; only $2000-$401F matter here). */
uint8_t nes_io_read(nes *n, uint16_t addr);
void nes_io_write(nes *n, uint16_t addr, uint8_t v);
void nes_vblank_start(nes *n);
void nes_prerender(nes *n);
void nes_poke(nes *n, uint16_t addr, uint8_t v);

#endif
