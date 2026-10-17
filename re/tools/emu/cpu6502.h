/* Minimal NMOS 6502 core (2A03 variant: no decimal mode) used as the
 * reference oracle for differential testing of the C reconstruction. */
#ifndef CPU6502_H
#define CPU6502_H
#include <stdint.h>

typedef struct cpu6502 {
    uint16_t pc;
    uint8_t a, x, y, sp, p;
    uint64_t cycles;
    int halted;            /* set on illegal opcode */
    int exec_phase;        /* 0 while fetching opcode/operands, 1 while executing */
    uint16_t halt_pc;
    uint8_t halt_op;
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t v);
    /* optional: called before each instruction fetch */
    void (*on_exec)(void *ctx, uint16_t pc);
} cpu6502;

enum { FC = 1, FZ = 2, FI = 4, FD = 8, FB = 16, FU = 32, FV = 64, FN = 128 };

void cpu_reset(cpu6502 *c);
void cpu_nmi(cpu6502 *c);
void cpu_irq(cpu6502 *c);
int cpu_step(cpu6502 *c); /* returns cycles consumed */

#endif
