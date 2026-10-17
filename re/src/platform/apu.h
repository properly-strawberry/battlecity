/* NES APU (2A03) sound synthesis for the game front end.
 *
 * Written from the documented hardware behaviour (NESdev wiki): two pulse
 * channels with envelope, sweep and length counter; triangle with linear
 * counter; noise with a 15-bit LFSR; the frame counter in 4- and 5-step
 * modes; the non-linear mixer; and the Famicom's output filters (high-pass
 * 37 Hz, low-pass 14 kHz; Battle City is a Famicom game, and the NES
 * front-loader's extra 440 Hz high-pass would remove the engine rumble). No DMC: Battle City never writes
 * $4010-$4013 (observed: register log of fceux_playthrough1).
 *
 * Time is counted in CPU cycles (NTSC, 1789773 Hz). The caller interleaves
 * apu_run (advance N cycles, producing samples) and apu_write. */
#ifndef BC_APU_H
#define BC_APU_H
#include <stdint.h>

#define APU_CPU_HZ 1789773.0

typedef struct {
    uint8_t enabled, length, halt;          /* length counter; halt = envelope loop */
    uint8_t env_start, env_div, env_decay, env_period, env_const;
    uint16_t period, timer;
    uint8_t duty, step;
    uint8_t sw_en, sw_period, sw_neg, sw_shift, sw_div, sw_reload;
    uint8_t ones_complement;                 /* pulse 1 negates with ones' complement */
} apu_pulse;

typedef struct {
    uint8_t enabled, length, control;        /* control = length halt + linear control */
    uint8_t lin_reload, lin_counter, lin_reload_flag;
    uint16_t period, timer;
    uint8_t step;
} apu_triangle;

typedef struct {
    uint8_t enabled, length, halt;
    uint8_t env_start, env_div, env_decay, env_period, env_const;
    uint8_t mode;
    uint16_t period, timer, lfsr;
} apu_noise;

typedef struct {
    apu_pulse p[2];
    apu_triangle t;
    apu_noise n;
    uint8_t five_step;                       /* $4017 bit 7 */
    long fc_cycle;                           /* CPU cycles since the frame counter reset */
    uint8_t fceux_timing, fc_step, fc_gap;   /* test only: FCEUX's frame-step timing (apu.c) */
    uint8_t mute;                            /* test only: bit 0/1 pulse 1/2, 2 triangle, 3 noise */
    uint8_t primed;                          /* output filters seeded with the first sample */
    uint8_t odd;                             /* CPU cycle parity: pulse/noise timers tick every 2nd */
    /* output: box-filter resampling, then the console filters */
    double rate, cyc_per_sample, acc, acc_n, phase;
    double hp_prev_in, hp_prev_out, lp_prev;
    double hp_k, lp_k;
} apu;

void apu_init(apu *a, int sample_rate);
void apu_write(apu *a, uint16_t addr, uint8_t v);   /* $4000-$4017 */
/* Advance `cycles` CPU cycles; append samples (about -1..1) to out, at most
 * max. Returns the number of samples written. */
int apu_run(apu *a, long cycles, float *out, int max);

#endif
