/* Sound engine ($EA51-$ECD0) and its data ($ECE6-$EFFF).
 *
 * Model (verified by these routines, see docs/subsystems/sound.md for the data format):
 *  - 28 "sound slots" (sound_req $0300+n, voice record $031C+8*n). Game code
 *    requests sound n by writing 1 to sound_req[n]. Slot number is also the
 *    priority: lower slots are served first.
 *  - Each voice record is 8 bytes (V0..V7):
 *      V0 hw channel+1 (1 sq1, 2 sq2, 3 tri, 4 noise); +4 once its APU
 *         registers have been loaded ("active"), 0 = nothing to play
 *      V1 reg0 (volume/duty)  V2 reg1 (sweep)  V3 reg2 (period lo)
 *      V4 reg3 (period hi | length counter)  V5 stream position
 *      V6 current note duration  V7 frames left in the current note
 *  - Sound script bytes live in ROM; table dat_ECFE gives the address of
 *    each slot's script.
 *  - Once per frame (NMI) sound_driver() (a) copies the registers of the
 *    first pending record per hardware channel to the APU, (b) silences
 *    channels nobody uses, and (c) advances every playing script.
 *  - Scratch: F2/F3 script pointer, F4 slot index, F5 slot count,
 *    F6-F8 loop counters, F9-FC "channel in use this frame", FD/FE temps. */
#include "common.h"

#define SLOT_COUNT      0x1C
#define VOICES          0x031C
#define script_ptr      0xF2            /* zp pointer to the slot's script */
#define chan_busy       0xF9            /* [4] zp base, indexed with wrap */

/* voice record field access through snd_ptr ($F0) */
#define V(i)       ind_y(0xF0, (i))
#define SETV(i, v) ind_y_store(0xF0, (i), (v))

/* $EA51 sound_reset: enable the 4 channels (not DMC), frame counter 5-step
 * with IRQ off, clear all 28 slot requests and voice byte 0. Leaves
 * snd_ptr = $03FC. */
void sound_reset(void) {
    hal_write(0x4015, 0x0F);
    hal_write(0x4017, 0xC0);
    set_zptr(0xF0, VOICES);
    for (uint8_t x = 0; x < SLOT_COUNT; x++) {
        ind_y_store(0xF0, 0, 0);
        sound_req[x] = 0;
        set_zptr(0xF0, (uint16_t)(ZPTR(0xF0) + 8));
    }
}
void sub_EA51(void) { sound_reset(); A = 0; X = SLOT_COUNT; Y = 0; fC = 1; fZ = 1; fN = 0; }

/* $ECAF point F2/F3 at the script of the current slot (table dat_ECFE) */
static void load_script_ptr(void) {
    uint8_t x = (uint8_t)(snd_slot << 1);
    snd_script_ptr[0] = bus_read((uint16_t)(0xECFE + x));
    snd_script_ptr[1] = bus_read((uint16_t)(0xECFF + x));
}
void sub_ECAF(void) {
    load_script_ptr();
    uint8_t x = (uint8_t)(snd_slot << 1);
    fC = snd_slot >> 7; X = x; A = snd_script_ptr[1]; SZN(A);
}

/* $ECBE fetch the next script byte of the current slot (V5 = position) and
 * advance V5. The original also leaves Y = 5. */
static uint8_t next_byte(void) {
    uint8_t pos = V(5);
    uint8_t b = ind_y(script_ptr, pos);
    SETV(5, (uint8_t)(pos + 1));
    return b;
}
void sub_ECBE(void) { A = next_byte(); SZN(A); Y = 5; }

/* what the script interpreter does after a command */
enum { CMD_CONTINUE, CMD_SLOT_DONE };

/* ---- script commands $E8-$F9 (handlers behind the inline table at $EBE6) ---- */

/* $EC0A cmd $E8 end of sound: free the slot and the voice */
static int cmd_end(void) {
    sound_req[snd_slot] = 0;
    SETV(0, 0);
    SETV(5, (uint8_t)(V(5) - 1));   /* leave the position on the $E8 byte */
    return CMD_SLOT_DONE;
}
/* $EC21 cmd $E9 n: duty bits (top 2 bits of V1) |= n */
static int cmd_duty(void) {
    tmp_fd = next_byte();
    SETV(1, (uint8_t)((V(1) & 0x3F) | tmp_fd));
    return CMD_CONTINUE;
}
/* $EC33 cmd $EA n: low 6 bits of V1 = n (envelope/volume), keeps duty */
static int cmd_vol6(void) {
    tmp_fd = next_byte();
    SETV(1, (uint8_t)((V(1) & 0xC0) | tmp_fd));
    return CMD_CONTINUE;
}
/* $EC45 cmd $EB n: low nibble of V1 = n (volume), keeps top nibble */
static int cmd_vol4(void) {
    tmp_fd = next_byte();
    SETV(1, (uint8_t)((V(1) & 0xF0) | tmp_fd));
    return CMD_CONTINUE;
}
/* $EC57 cmd $EC n: V2 (sweep register) = n */
static int cmd_sweep(void) { SETV(2, next_byte()); return CMD_CONTINUE; }
/* $EC61 cmd $ED n: V4 (period hi / length register) = n */
static int cmd_reg3(void) { SETV(4, next_byte()); return CMD_CONTINUE; }
/* $EC6B cmd $EE n: V1 (volume/duty register) = n */
static int cmd_reg0(void) { SETV(1, next_byte()); return CMD_CONTINUE; }
/* $EC75 cmd $EF: clear the three loop counters */
static int cmd_reset_loops(void) {
    for (int x = 2; x >= 0; x--) loop_count[x] = 0;
    return CMD_CONTINUE;
}
/* $EC99 skip one operand byte (cmds $F3-$F8 take one byte and ignore it) */
static int cmd_skip(void) { SETV(5, (uint8_t)(V(5) + 1)); return CMD_CONTINUE; }
/* $ECA5 cmd $F9 pos: jump, V5 = pos */
static int cmd_jump(void) { SETV(5, next_byte()); return CMD_CONTINUE; }
/* $EC81/$EC85/$EC88 cmds $F0/$F1/$F2 count pos: loop with counter n.
 * Each pass the counter is incremented; while it differs from count the
 * script jumps to pos, when equal the counter resets and the jump operand
 * is skipped (falls through). */
static int cmd_loop(uint8_t n) {
    uint8_t count = next_byte();
    loop_count[n]++;
    if (count != loop_count[n]) return cmd_jump();
    loop_count[n] = 0;
    return cmd_skip();
}

/* Note period table dat_ECE6: 12 big-endian words (hi, lo), highest octave
 * first: index = note>>3; the low 3 bits of the note byte shift the period
 * right (octave up). */

/* $EB88 script interpreter: run commands of the current slot until a note
 * or rest ends the frame's work for this slot (or the sound ends). */
static void run_script(void) {
    for (;;) {
        uint8_t b = next_byte();
        if (b >= 0xE8) {
            /* $ECD0 jump through the inline table, in the original with
             * index (b-$E8). Leaves the target address in FD/FE. */
            uint8_t n = (uint8_t)(b - 0xE8);
            uint8_t y = (uint8_t)((n << 1) + 1);
            uint8_t lo = bus_read((uint16_t)(0xEBE5 + y));
            uint8_t hi = bus_read((uint16_t)(0xEBE5 + y + 1));
            tmp_fd = lo;
            tmp_fe = hi;
            int r;
            switch (n) {
            case 0:  r = cmd_end(); break;
            case 1:  r = cmd_duty(); break;
            case 2:  r = cmd_vol6(); break;
            case 3:  r = cmd_vol4(); break;
            case 4:  r = cmd_sweep(); break;
            case 5:  r = cmd_reg3(); break;
            case 6:  r = cmd_reg0(); break;
            case 7:  r = cmd_reset_loops(); break;
            case 8:  r = cmd_loop(0); break;
            case 9:  r = cmd_loop(1); break;
            case 10: r = cmd_loop(2); break;
            case 11: case 12: case 13: case 14: case 15: case 16:
                r = cmd_skip(); break;
            case 17: r = cmd_jump(); break;
            default:
                /* QUIRK: commands $FA-$FF index past the 18-entry table and
                 * would jump into the middle of the code. Never occurs with
                 * the shipped script data; not reproduced. */
                r = CMD_SLOT_DONE; break;
            }
            if (r == CMD_SLOT_DONE) return;
            continue;
        }
        if (b == 0x60) break;                       /* rest: keep the registers */
        if (b > 0x60) {                             /* $61-$E7: set duration */
            SETV(6, (uint8_t)(b - 0x60));
            continue;
        }
        /* note $00-$5F: period = table[b>>3] >> (b&7) */
        uint8_t idx = (uint8_t)((b & 0xF8) >> 2);
        tmp_fd = bus_read((uint16_t)(0xECE6 + idx));
        tmp_fe = bus_read((uint16_t)(0xECE7 + idx));
        for (uint8_t sh = b & 7; sh; sh--) {
            uint8_t carry = tmp_fd & 1;
            tmp_fd >>= 1;
            tmp_fe = (uint8_t)((tmp_fe >> 1) | (carry << 7));
        }
        SETV(4, (uint8_t)((V(4) & 0xF8) | tmp_fd));
        SETV(3, tmp_fe);
        uint8_t v0 = V(0);
        if (v0 >= 5) SETV(0, (uint8_t)(v0 - 4));    /* mark: reload APU regs next frame */
        break;
    }
    SETV(7, V(6));                                  /* $EBD7: start the note timer */
}

/* header of a freshly started sound: channel, V1, V2, V4, (noise: V3) */
static void start_sound(void) {
    SETV(5, 0);
    load_script_ptr();
    SETV(0, next_byte());
    SETV(1, next_byte());
    SETV(2, next_byte());
    SETV(4, next_byte());
    if (V(0) == 4) SETV(3, next_byte());
}

/* the per-slot advance loop ($EB1D). from_next = resume after the slot
 * body (used by the standalone command adapters). */
static void advance_slots(int skip_first_body) {
    for (;;) {
        if (!skip_first_body) {
            uint8_t req = sound_req[snd_slot];
            if (req != 0) {
                if (req == 1) {
                    sound_req[snd_slot] = 2;
                    start_sound();
                    run_script();
                } else {
                    uint8_t left = (uint8_t)(V(7) - 1);
                    SETV(7, left);
                    if (left == 0) {
                        load_script_ptr();
                        run_script();
                    }
                }
            }
        }
        skip_first_body = 0;
        set_zptr(0xF0, (uint16_t)(ZPTR(0xF0) + 8));
        snd_slot++;
        if (!(snd_slot < snd_slot_limit)) return;
    }
}

/* $EA7E sound_driver (called from the NMI every frame). */
void sound_driver(void) {
    snd_slot_limit = paused ? 0x01 : SLOT_COUNT;    /* paused: only slot 0 */
    for (int x = 3; x >= 0; x--) RAM[(uint8_t)(chan_busy + x)] = 0;
    snd_slot = 0;
    set_zptr(0xF0, VOICES);
    /* pass 1: hand each hardware channel to the first (lowest) slot that
     * wants it and load its 4 registers into the APU */
    do {
        if (sound_req[snd_slot] != 0) {
            uint8_t v0 = V(0);
            if (v0 != 0) {
                if (v0 >= 5) {
                    RAM[(uint8_t)(chan_busy + (uint8_t)(v0 - 5))] = 1;   /* already loaded */
                } else {
                    uint8_t ch = (uint8_t)(v0 - 1);
                    if (RAM[(uint8_t)(chan_busy + ch)] == 0) {
                        RAM[(uint8_t)(chan_busy + ch)] = 1;
                        SETV(0, (uint8_t)(ch + 5));
                        uint8_t reg = (uint8_t)(ch << 2);
                        for (uint8_t i = 1; i <= 4; i++)
                            bus_write((uint16_t)(0x4000 + reg++), V(i));
                        tmp_fd = 0;
                    }
                }
            }
        }
        set_zptr(0xF0, (uint16_t)(ZPTR(0xF0) + 8));
        snd_slot++;
    } while (snd_slot < snd_slot_limit);
    /* pass 2: silence unused channels: sq1/sq2/noise vol reg = $10
     * (const volume 0), triangle linear counter = 0 */
    for (uint8_t ch = 0; ch < 4; ch++) {
        tmp_fd = ch;
        if (RAM[(uint8_t)(chan_busy + ch)] == 0)
            bus_write((uint16_t)(0x4000 + (ch << 2)), (uint8_t)(((ch << 3) & 0x10) ^ 0x10));
    }
    /* pass 3: run the scripts */
    snd_slot = 0;
    set_zptr(0xF0, VOICES);
    /* (the original's loop starts with sound_req test; slot 0 is always valid) */
    advance_slots(0);
}
void sub_EA7E(void) { sound_driver(); }

/* ---- standalone entries into the middle of the driver ----
 * Each command handler is also a routine address in the original (the
 * inline table targets). They continue the interpreter, then the slot loop,
 * until the driver would return. */
static void cmd_standalone(int (*cmd)(void)) {
    if (cmd() == CMD_SLOT_DONE) { advance_slots(1); return; }
    run_script();
    advance_slots(1);
}
void sub_EC0A(void) { cmd_standalone(cmd_end); }
void sub_EC33(void) { cmd_standalone(cmd_vol6); }
void sub_EC6B(void) { cmd_standalone(cmd_reg0); }
void sub_EC75(void) { cmd_standalone(cmd_reset_loops); }
static int cmd_loop0(void) { return cmd_loop(0); }
static int cmd_loop1(void) { return cmd_loop(1); }
static int cmd_loop2(void) { return cmd_loop(2); }
void sub_EC81(void) { cmd_standalone(cmd_loop0); }
void sub_EC85(void) { cmd_standalone(cmd_loop1); }
void sub_EC88(void) { cmd_standalone(cmd_loop2); }
void sub_EC99(void) { cmd_standalone(cmd_skip); }
void sub_ECA5(void) { cmd_standalone(cmd_jump); }

/* $EC21/$EC45/$EC57/$EC61: commands $E9/$EB/$EC/$ED. No shipped script
 * reaches them (a walk of all 28 scripts in dat_ECFE, following jumps and
 * both loop exits, finds only $E8 $EA $EE $EF $F0 $F1 $F2 $F9), so no movie
 * runs them. difftest tests them from the entry states of sibling handlers
 * ($EC33 / $EC6B: same dispatcher, same context), see tools/difftest.py. */
void sub_EC21(void) { cmd_standalone(cmd_duty); }
void sub_EC45(void) { cmd_standalone(cmd_vol4); }
void sub_EC57(void) { cmd_standalone(cmd_sweep); }
void sub_EC61(void) { cmd_standalone(cmd_reg3); }

/* $ECD0 inline dispatch: index A selects a word from the table that
 * follows the caller's JSR; the caller's return address is popped and the
 * routine jumps to the selected target (never returns to the caller).
 * The only caller ($EBE3) is inside run_script(), which does this in C, so
 * this adapter serves register-level callers only. difftest cannot run it
 * standalone (it reads its caller's return address). */
void sub_ECD0(void) {
    uint8_t y = (uint8_t)((A << 1) + 1);
    uint8_t lo = PULL();
    uint8_t hi = PULL();
    uint16_t table = (uint16_t)(lo | (hi << 8));     /* return address - 1 */
    uint8_t tlo = bus_read((uint16_t)(table + y));
    uint8_t thi = bus_read((uint16_t)(table + y + 1));
    tmp_fd = tlo; tmp_fe = thi;
    X = tlo; Y = (uint8_t)(y + 1); A = thi; SZN(A);
    mech_dispatch((uint16_t)(tlo | (thi << 8)));
}
