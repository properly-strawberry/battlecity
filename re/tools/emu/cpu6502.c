#include "cpu6502.h"

#define RD(a) c->read(c->ctx, (uint16_t)(a))
#define WR(a, v) c->write(c->ctx, (uint16_t)(a), (uint8_t)(v))

static void push(cpu6502 *c, uint8_t v) { WR(0x100 | c->sp, v); c->sp--; }
static uint8_t pull(cpu6502 *c) { c->sp++; return RD(0x100 | c->sp); }
static uint16_t rd16(cpu6502 *c, uint16_t a) { return RD(a) | (RD(a + 1) << 8); }
/* JMP (ind) page-wrap bug */
static uint16_t rd16_bug(cpu6502 *c, uint16_t a) {
    uint16_t hi = (a & 0xFF00) | ((a + 1) & 0x00FF);
    return RD(a) | (RD(hi) << 8);
}
static void setzn(cpu6502 *c, uint8_t v) {
    c->p = (c->p & ~(FZ | FN)) | (v ? 0 : FZ) | (v & 0x80);
}

void cpu_reset(cpu6502 *c) {
    c->a = c->x = c->y = 0;
    c->sp = 0xFD;
    c->p = FI | FU;
    c->pc = rd16(c, 0xFFFC);
    c->halted = 0;
    c->cycles += 7;
}

static void interrupt(cpu6502 *c, uint16_t vec, int brk) {
    push(c, c->pc >> 8);
    push(c, c->pc & 0xFF);
    push(c, (c->p | FU | (brk ? FB : 0)) & (brk ? 0xFF : ~FB));
    c->p |= FI;
    c->pc = rd16(c, vec);
    c->cycles += 7;
}
void cpu_nmi(cpu6502 *c) { interrupt(c, 0xFFFA, 0); }
void cpu_irq(cpu6502 *c) { if (!(c->p & FI)) interrupt(c, 0xFFFE, 0); }

enum { IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL };

/* mode, base cycles, page-cross penalty applies */
static const uint8_t mode_tab[256] = {
/*        0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F */
/*0*/   IMP, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, ACC, IMM, ABS, ABS, ABS, ABS,
/*1*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
/*2*/   ABS, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, ACC, IMM, ABS, ABS, ABS, ABS,
/*3*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
/*4*/   IMP, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, ACC, IMM, ABS, ABS, ABS, ABS,
/*5*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
/*6*/   IMP, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, ACC, IMM, IND, ABS, ABS, ABS,
/*7*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
/*8*/   IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
/*9*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPY, ZPY, IMP, ABY, IMP, ABY, ABX, ABX, ABY, ABY,
/*A*/   IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
/*B*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPY, ZPY, IMP, ABY, IMP, ABY, ABX, ABX, ABY, ABY,
/*C*/   IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
/*D*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
/*E*/   IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
/*F*/   REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
};
static const uint8_t cyc_tab[256] = {
    7,6,0,8,3,3,5,5,3,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
    6,6,0,8,3,3,5,5,4,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
    6,6,0,8,3,3,5,5,3,2,2,2,3,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
    6,6,0,8,3,3,5,5,4,2,2,2,5,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
    2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,6,0,6,4,4,4,4,2,5,2,5,5,5,5,5,
    2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,5,0,5,4,4,4,4,2,4,2,4,4,4,4,4,
    2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
    2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
};
/* read-type ops that pay +1 on page cross for ABX/ABY/IZY */
static int pays_cross(uint8_t op) {
    switch (op & 0x1F) {
    case 0x11: case 0x19: case 0x1D: case 0x1C: case 0x1E: case 0x1F: case 0x1B: case 0x13:
        break;
    default: return 0;
    }
    /* stores and RMW never pay */
    switch (op) {
    case 0x91: case 0x99: case 0x9D: case 0x9C: case 0x9E: case 0x9F: case 0x9B: case 0x93:
    case 0x1E: case 0x3E: case 0x5E: case 0x7E: case 0xDE: case 0xFE:
    case 0x1F: case 0x3F: case 0x5F: case 0x7F: case 0xDF: case 0xFF:
    case 0x1B: case 0x3B: case 0x5B: case 0x7B: case 0xDB: case 0xFB:
    case 0x13: case 0x33: case 0x53: case 0x73: case 0xD3: case 0xF3:
        return 0;
    }
    return 1;
}

static int is_official(uint8_t op) {
    switch (op) {
    case 0x00: case 0x01: case 0x05: case 0x06: case 0x08: case 0x09: case 0x0A: case 0x0D: case 0x0E:
    case 0x10: case 0x11: case 0x15: case 0x16: case 0x18: case 0x19: case 0x1D: case 0x1E:
    case 0x20: case 0x21: case 0x24: case 0x25: case 0x26: case 0x28: case 0x29: case 0x2A: case 0x2C: case 0x2D: case 0x2E:
    case 0x30: case 0x31: case 0x35: case 0x36: case 0x38: case 0x39: case 0x3D: case 0x3E:
    case 0x40: case 0x41: case 0x45: case 0x46: case 0x48: case 0x49: case 0x4A: case 0x4C: case 0x4D: case 0x4E:
    case 0x50: case 0x51: case 0x55: case 0x56: case 0x58: case 0x59: case 0x5D: case 0x5E:
    case 0x60: case 0x61: case 0x65: case 0x66: case 0x68: case 0x69: case 0x6A: case 0x6C: case 0x6D: case 0x6E:
    case 0x70: case 0x71: case 0x75: case 0x76: case 0x78: case 0x79: case 0x7D: case 0x7E:
    case 0x81: case 0x84: case 0x85: case 0x86: case 0x88: case 0x8A: case 0x8C: case 0x8D: case 0x8E:
    case 0x90: case 0x91: case 0x94: case 0x95: case 0x96: case 0x98: case 0x99: case 0x9A: case 0x9D:
    case 0xA0: case 0xA1: case 0xA2: case 0xA4: case 0xA5: case 0xA6: case 0xA8: case 0xA9: case 0xAA: case 0xAC: case 0xAD: case 0xAE:
    case 0xB0: case 0xB1: case 0xB4: case 0xB5: case 0xB6: case 0xB8: case 0xB9: case 0xBA: case 0xBC: case 0xBD: case 0xBE:
    case 0xC0: case 0xC1: case 0xC4: case 0xC5: case 0xC6: case 0xC8: case 0xC9: case 0xCA: case 0xCC: case 0xCD: case 0xCE:
    case 0xD0: case 0xD1: case 0xD5: case 0xD6: case 0xD8: case 0xD9: case 0xDD: case 0xDE:
    case 0xE0: case 0xE1: case 0xE4: case 0xE5: case 0xE6: case 0xE8: case 0xE9: case 0xEA: case 0xEC: case 0xED: case 0xEE:
    case 0xF0: case 0xF1: case 0xF5: case 0xF6: case 0xF8: case 0xF9: case 0xFD: case 0xFE:
        return 1;
    }
    return 0;
}

static void adc(cpu6502 *c, uint8_t v) {
    unsigned s = c->a + v + (c->p & FC);
    c->p &= ~(FC | FV);
    if (s > 0xFF) c->p |= FC;
    if (~(c->a ^ v) & (c->a ^ s) & 0x80) c->p |= FV;
    c->a = (uint8_t)s;
    setzn(c, c->a);
}
static void cmp(cpu6502 *c, uint8_t r, uint8_t v) {
    c->p = (c->p & ~FC) | (r >= v ? FC : 0);
    setzn(c, (uint8_t)(r - v));
}

int cpu_step(cpu6502 *c) {
    if (c->halted) return 0;
    if (c->on_exec) c->on_exec(c->ctx, c->pc);
    uint16_t opc_pc = c->pc;
    c->exec_phase = 0;
    uint8_t op = RD(c->pc++);
    if (!is_official(op)) {
        c->halted = 1; c->halt_pc = opc_pc; c->halt_op = op;
        return 0;
    }
    int cyc = cyc_tab[op];
    uint16_t ea = 0;
    int m = mode_tab[op];
    switch (m) {
    case IMP: case ACC: break;
    case IMM: ea = c->pc++; break;
    case ZP: ea = RD(c->pc++); break;
    case ZPX: ea = (RD(c->pc++) + c->x) & 0xFF; break;
    case ZPY: ea = (RD(c->pc++) + c->y) & 0xFF; break;
    case ABS: ea = rd16(c, c->pc); c->pc += 2; break;
    case ABX: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = b + c->x;
        if (((b ^ ea) & 0xFF00) && pays_cross(op)) cyc++;
        break; }
    case ABY: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = b + c->y;
        if (((b ^ ea) & 0xFF00) && pays_cross(op)) cyc++;
        break; }
    case IND: ea = rd16_bug(c, rd16(c, c->pc)); c->pc += 2; break;
    case IZX: { uint8_t z = RD(c->pc++) + c->x; ea = RD(z) | (RD((uint8_t)(z + 1)) << 8); break; }
    case IZY: { uint8_t z = RD(c->pc++); uint16_t b = RD(z) | (RD((uint8_t)(z + 1)) << 8);
        ea = b + c->y; if (((b ^ ea) & 0xFF00) && pays_cross(op)) cyc++; break; }
    case REL: ea = (uint16_t)(c->pc + 1 + (int8_t)RD(c->pc)); c->pc++; break;
    }

    c->exec_phase = 1;
#define BR(cond) if (cond) { cyc += 1 + (((c->pc ^ ea) & 0xFF00) ? 1 : 0); c->pc = ea; } break
    uint8_t v;
    switch (op) {
    /* loads/stores */
    case 0xA9: case 0xA5: case 0xB5: case 0xAD: case 0xBD: case 0xB9: case 0xA1: case 0xB1:
        c->a = RD(ea); setzn(c, c->a); break;
    case 0xA2: case 0xA6: case 0xB6: case 0xAE: case 0xBE:
        c->x = RD(ea); setzn(c, c->x); break;
    case 0xA0: case 0xA4: case 0xB4: case 0xAC: case 0xBC:
        c->y = RD(ea); setzn(c, c->y); break;
    case 0x85: case 0x95: case 0x8D: case 0x9D: case 0x99: case 0x81: case 0x91: WR(ea, c->a); break;
    case 0x86: case 0x96: case 0x8E: WR(ea, c->x); break;
    case 0x84: case 0x94: case 0x8C: WR(ea, c->y); break;
    /* transfers */
    case 0xAA: c->x = c->a; setzn(c, c->x); break;
    case 0xA8: c->y = c->a; setzn(c, c->y); break;
    case 0x8A: c->a = c->x; setzn(c, c->a); break;
    case 0x98: c->a = c->y; setzn(c, c->a); break;
    case 0xBA: c->x = c->sp; setzn(c, c->x); break;
    case 0x9A: c->sp = c->x; break;
    /* stack */
    case 0x48: push(c, c->a); break;
    case 0x08: push(c, c->p | FB | FU); break;
    case 0x68: c->a = pull(c); setzn(c, c->a); break;
    case 0x28: c->p = (pull(c) & ~FB) | FU; break;
    /* logic */
    case 0x29: case 0x25: case 0x35: case 0x2D: case 0x3D: case 0x39: case 0x21: case 0x31:
        c->a &= RD(ea); setzn(c, c->a); break;
    case 0x09: case 0x05: case 0x15: case 0x0D: case 0x1D: case 0x19: case 0x01: case 0x11:
        c->a |= RD(ea); setzn(c, c->a); break;
    case 0x49: case 0x45: case 0x55: case 0x4D: case 0x5D: case 0x59: case 0x41: case 0x51:
        c->a ^= RD(ea); setzn(c, c->a); break;
    case 0x24: case 0x2C: v = RD(ea);
        c->p = (c->p & ~(FZ | FV | FN)) | ((c->a & v) ? 0 : FZ) | (v & 0xC0); break;
    /* arithmetic */
    case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71:
        adc(c, RD(ea)); break;
    case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1:
        adc(c, RD(ea) ^ 0xFF); break;
    case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD: case 0xD9: case 0xC1: case 0xD1:
        cmp(c, c->a, RD(ea)); break;
    case 0xE0: case 0xE4: case 0xEC: cmp(c, c->x, RD(ea)); break;
    case 0xC0: case 0xC4: case 0xCC: cmp(c, c->y, RD(ea)); break;
    /* inc/dec */
    case 0xE6: case 0xF6: case 0xEE: case 0xFE: v = RD(ea); WR(ea, v); v++; WR(ea, v); setzn(c, v); break;
    case 0xC6: case 0xD6: case 0xCE: case 0xDE: v = RD(ea); WR(ea, v); v--; WR(ea, v); setzn(c, v); break;
    case 0xE8: c->x++; setzn(c, c->x); break;
    case 0xCA: c->x--; setzn(c, c->x); break;
    case 0xC8: c->y++; setzn(c, c->y); break;
    case 0x88: c->y--; setzn(c, c->y); break;
    /* shifts */
    case 0x0A: c->p = (c->p & ~FC) | (c->a >> 7); c->a <<= 1; setzn(c, c->a); break;
    case 0x4A: c->p = (c->p & ~FC) | (c->a & 1); c->a >>= 1; setzn(c, c->a); break;
    case 0x2A: { uint8_t ci = c->p & FC; c->p = (c->p & ~FC) | (c->a >> 7); c->a = (c->a << 1) | ci; setzn(c, c->a); break; }
    case 0x6A: { uint8_t ci = (c->p & FC) << 7; c->p = (c->p & ~FC) | (c->a & 1); c->a = (c->a >> 1) | ci; setzn(c, c->a); break; }
    case 0x06: case 0x16: case 0x0E: case 0x1E: v = RD(ea); WR(ea, v);
        c->p = (c->p & ~FC) | (v >> 7); v <<= 1; WR(ea, v); setzn(c, v); break;
    case 0x46: case 0x56: case 0x4E: case 0x5E: v = RD(ea); WR(ea, v);
        c->p = (c->p & ~FC) | (v & 1); v >>= 1; WR(ea, v); setzn(c, v); break;
    case 0x26: case 0x36: case 0x2E: case 0x3E: { v = RD(ea); WR(ea, v); uint8_t ci = c->p & FC;
        c->p = (c->p & ~FC) | (v >> 7); v = (v << 1) | ci; WR(ea, v); setzn(c, v); break; }
    case 0x66: case 0x76: case 0x6E: case 0x7E: { v = RD(ea); WR(ea, v); uint8_t ci = (c->p & FC) << 7;
        c->p = (c->p & ~FC) | (v & 1); v = (v >> 1) | ci; WR(ea, v); setzn(c, v); break; }
    /* jumps */
    case 0x4C: case 0x6C: c->pc = ea; break;
    case 0x20: { uint16_t r = c->pc - 1; push(c, r >> 8); push(c, r & 0xFF); c->pc = ea; break; }
    case 0x60: { uint16_t lo = pull(c); uint16_t hi = pull(c); c->pc = ((hi << 8) | lo) + 1; break; }
    case 0x40: { c->p = (pull(c) & ~FB) | FU; uint16_t lo = pull(c); uint16_t hi = pull(c); c->pc = (hi << 8) | lo; break; }
    case 0x00: c->pc++; interrupt(c, 0xFFFE, 1); cyc = 0; break;
    /* branches */
    case 0x10: BR(!(c->p & FN));
    case 0x30: BR(c->p & FN);
    case 0x50: BR(!(c->p & FV));
    case 0x70: BR(c->p & FV);
    case 0x90: BR(!(c->p & FC));
    case 0xB0: BR(c->p & FC);
    case 0xD0: BR(!(c->p & FZ));
    case 0xF0: BR(c->p & FZ);
    /* flags */
    case 0x18: c->p &= ~FC; break;
    case 0x38: c->p |= FC; break;
    case 0x58: c->p &= ~FI; break;
    case 0x78: c->p |= FI; break;
    case 0xB8: c->p &= ~FV; break;
    case 0xD8: c->p &= ~FD; break;
    case 0xF8: c->p |= FD; break;
    case 0xEA: break;
    }
    c->cycles += cyc;
    return cyc;
}
