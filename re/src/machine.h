/* Battle City reconstruction: machine state shared by all C code.
 *
 * The reconstruction keeps the original memory layout: CPU RAM ($0000-$07FF)
 * is one array, and every game variable stays at its original address. Then
 * RAM images from the original ROM and from this code can be compared byte
 * for byte (see re/docs/verification.md).
 *
 * The 6502 registers are globals too. Only the mechanical layer (src/mech)
 * and register-level adapters use them; lifted code passes values through
 * real C parameters. */
#ifndef BC_MACHINE_H
#define BC_MACHINE_H
#include <stdint.h>

extern uint8_t RAM[0x800];
extern uint8_t PRG[0x4000];   /* original PRG bytes, $C000-$FFFF: filled at
                                * start-up by bc_rom_load (rom.h), never embedded */

/* 6502 register file (used by mechanical code and adapters) */
extern uint8_t A, X, Y, S;
extern uint8_t fC, fZ, fN, fV, fI, fD;   /* each 0 or 1 */

/* ---- hardware abstraction (implemented by the platform/harness) ---- */
uint8_t hal_read(uint16_t addr);           /* $2000-$401F */
void hal_write(uint16_t addr, uint8_t v);  /* $2000-$401F */
/* Called where the original spins waiting for vblank (frame-counter loop at
 * $D8F8, PPUSTATUS polls at $C079/$D5F5). Must advance to the next vblank,
 * set PPUSTATUS bit 7, and run the NMI handler (bc_nmi) when PPUCTRL bit 7
 * is set. */
void hal_wait_vblank(void);

/* full CPU address space, as the original sees it */
static inline uint8_t bus_read(uint16_t a) {
    if (a < 0x2000) return RAM[a & 0x7FF];
    if (a >= 0x8000) return PRG[a & 0x3FFF];
    return hal_read(a);
}
static inline void bus_write(uint16_t a, uint8_t v) {
    if (a < 0x2000) RAM[a & 0x7FF] = v;
    else if (a < 0x8000) hal_write(a, v);
    /* writes to ROM are ignored (NROM) */
}

/* entry points */
void bc_reset(void);   /* never returns */
void bc_nmi(void);     /* NMI handler; called by hal_wait_vblank */

#endif
