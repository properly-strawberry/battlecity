/* Shared helpers for the lifted (hand-written) C code.
 *
 * Conventions (see docs/lifting_guide.md):
 *  - every lifted routine is a normal C function with real parameters and
 *    return values, named after what it does, with its original address in
 *    the comment above it;
 *  - each one also has a register adapter `void sub_XXXX(void)` so that
 *    not-yet-lifted mechanical code (and the difftest harness) can still call
 *    it with the 6502 register contract from docs/contracts.json;
 *  - side effects on RAM must match the original exactly, including scratch
 *    bytes. Mark anything surprising with QUIRK:. */
#ifndef BC_LIFTED_COMMON_H
#define BC_LIFTED_COMMON_H
#include "../ram.h"
#include "../tables.h"   /* ROM data tables: views into PRG[] (tools/gen_tables.py) */

/* Adapters a work package may override (make agent MOD=...) are weak.
 * PE/COFF linkers (MinGW) do not resolve weak definitions across objects,
 * so the Windows game build defines BC_NO_WEAK; it overrides nothing. */
#ifdef BC_NO_WEAK
#define BC_WEAK
#else
#define BC_WEAK __attribute__((weak))
#endif
#include "../mech/rt.h"   /* register file + SZN etc. for adapters only */

/* 16-bit little-endian pointer stored in two consecutive zero-page bytes */
#define ZPTR(lo_addr) ((uint16_t)(RAM[(uint8_t)(lo_addr)] | (RAM[(uint8_t)((lo_addr) + 1)] << 8)))
static inline void set_zptr(uint8_t lo_addr, uint16_t v) {
    RAM[lo_addr] = (uint8_t)v;
    RAM[(uint8_t)(lo_addr + 1)] = (uint8_t)(v >> 8);
}

/* (zp),y addressing: the pointer may target RAM or ROM */
static inline uint8_t ind_y(uint8_t zp, uint8_t y) { return bus_read((uint16_t)(ZPTR(zp) + y)); }
static inline void ind_y_store(uint8_t zp, uint8_t y, uint8_t v) { bus_write((uint16_t)(ZPTR(zp) + y), v); }

#endif
