/* Runtime for the mechanically translated 6502 code (src/mech/mech.c).
 * Every macro implements one 6502 instruction exactly (2A03: no decimal mode).
 * Stack operations use RAM page 1, as the CPU does, so RAM images match. */
#ifndef BC_MECH_RT_H
#define BC_MECH_RT_H
#include "../machine.h"

#define RD(a) bus_read((uint16_t)(a))
#define WR(a, v) bus_write((uint16_t)(a), (uint8_t)(v))
#define ZP16(z) (uint16_t)(RAM[(uint8_t)(z)] | (RAM[(uint8_t)((z) + 1)] << 8))

#define SZN(v) do { uint8_t _v = (uint8_t)(v); fZ = (_v == 0); fN = _v >> 7; } while (0)

static inline void PUSH(uint8_t v) { RAM[0x100 | S] = v; S--; }
static inline uint8_t PULL(void) { S++; return RAM[0x100 | S]; }
static inline uint8_t GETP(void) {
    return (uint8_t)(fC | fZ << 1 | fI << 2 | fD << 3 | 0x30 | fV << 6 | fN << 7);
}
static inline void SETP(uint8_t p) {
    fC = p & 1; fZ = (p >> 1) & 1; fI = (p >> 2) & 1; fD = (p >> 3) & 1;
    fV = (p >> 6) & 1; fN = p >> 7;
}

static inline void op_adc(uint8_t v) {
    unsigned s = A + v + fC;
    fV = ((~(A ^ v) & (A ^ s)) >> 7) & 1;
    fC = s > 0xFF;
    A = (uint8_t)s; SZN(A);
}
static inline void op_sbc(uint8_t v) { op_adc(v ^ 0xFF); }
static inline void op_cmp(uint8_t r, uint8_t v) { fC = r >= v; SZN(r - v); }
static inline void op_bit(uint8_t v) { fZ = (A & v) == 0; fN = v >> 7; fV = (v >> 6) & 1; }
static inline uint8_t op_asl(uint8_t v) { fC = v >> 7; v <<= 1; SZN(v); return v; }
static inline uint8_t op_lsr(uint8_t v) { fC = v & 1; v >>= 1; SZN(v); return v; }
static inline uint8_t op_rol(uint8_t v) { uint8_t c = fC; fC = v >> 7; v = (uint8_t)(v << 1 | c); SZN(v); return v; }
static inline uint8_t op_ror(uint8_t v) { uint8_t c = fC; fC = v & 1; v = (uint8_t)(v >> 1 | c << 7); SZN(v); return v; }

/* read-modify-write on memory: the CPU writes the old value back first;
 * this only matters for I/O registers, which this game never RMWs. */
#define RMW(a, fn) do { uint16_t _a = (uint16_t)(a); WR(_a, fn(RD(_a))); } while (0)
static inline uint8_t op_inc(uint8_t v) { v++; SZN(v); return v; }
static inline uint8_t op_dec(uint8_t v) { v--; SZN(v); return v; }

/* JSR: push the return address (address of the JSR's last byte) and call.
 * If the callee consumed our own return frame (PLA PLA tricks or the
 * $ECD0 inline dispatcher), the stack is not back where it was. Then
 * the original returns to our caller too, so we return immediately. */
#define JSR(ret, fn) do { uint8_t _s0 = S; PUSH((ret) >> 8); PUSH((ret) & 0xFF); \
                          fn(); if (S != _s0) return; } while (0)
#define RTS() do { S += 2; return; } while (0)
#define RTI() do { SETP(PULL()); S += 2; return; } while (0)

#ifdef MECH_TRACE
extern volatile uint16_t mech_last_pc;
#define MTRACE(a) (mech_last_pc = (a))
#else
#define MTRACE(a) ((void)0)
#endif

void mech_dispatch(uint16_t addr);  /* JMP (ind): tail call by address */

#endif
