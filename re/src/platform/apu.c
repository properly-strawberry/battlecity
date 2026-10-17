/* NES APU synthesis; see apu.h. */
#include "apu.h"
#include <math.h>
#include <string.h>

static const uint8_t length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};
static const uint8_t duty_table[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
};
static const uint8_t tri_table[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};
/* NTSC noise periods in CPU cycles */
static const uint16_t noise_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

void apu_init(apu *a, int sample_rate) {
    memset(a, 0, sizeof *a);
    a->p[0].ones_complement = 1;
    a->n.lfsr = 1;
    a->rate = sample_rate;
    a->cyc_per_sample = APU_CPU_HZ / sample_rate;
    double dt = 1.0 / sample_rate;
    double rc_hp = 1.0 / (2 * M_PI * 37), rc_lp = 1.0 / (2 * M_PI * 14000);
    a->hp_k = rc_hp / (rc_hp + dt);
    a->lp_k = dt / (rc_lp + dt);
}

/* ---- envelope / length / sweep ---- */
static void env_clock(uint8_t *start, uint8_t *div, uint8_t *decay, uint8_t period, uint8_t loop) {
    if (*start) { *start = 0; *decay = 15; *div = period; return; }
    if (*div) { (*div)--; return; }
    *div = period;
    if (*decay) (*decay)--;
    else if (loop) *decay = 15;
}

static uint16_t sweep_target(const apu_pulse *p) {
    uint16_t change = p->period >> p->sw_shift;
    if (!p->sw_neg) return (uint16_t)(p->period + change);
    return (uint16_t)(p->period - change - (p->ones_complement ? 1 : 0));
}
static int pulse_muted(const apu_pulse *p) {
    return p->period < 8 || (!p->sw_neg && sweep_target(p) > 0x7FF);
}
static void sweep_clock(apu_pulse *p) {
    if (p->sw_div == 0 && p->sw_en && p->sw_shift && !pulse_muted(p))
        p->period = sweep_target(p) & 0x7FF;
    if (p->sw_div == 0 || p->sw_reload) { p->sw_div = p->sw_period; p->sw_reload = 0; }
    else p->sw_div--;
}

static void quarter_frame(apu *a) {
    for (int i = 0; i < 2; i++) {
        apu_pulse *p = &a->p[i];
        env_clock(&p->env_start, &p->env_div, &p->env_decay, p->env_period, p->halt);
    }
    env_clock(&a->n.env_start, &a->n.env_div, &a->n.env_decay, a->n.env_period, a->n.halt);
    apu_triangle *t = &a->t;
    if (t->lin_reload_flag) t->lin_counter = t->lin_reload;
    else if (t->lin_counter) t->lin_counter--;
    if (!t->control) t->lin_reload_flag = 0;
}
static void half_frame(apu *a) {
    for (int i = 0; i < 2; i++) {
        apu_pulse *p = &a->p[i];
        if (!p->halt && p->length) p->length--;
        sweep_clock(p);
    }
    if (!a->t.control && a->t.length) a->t.length--;
    if (!a->n.halt && a->n.length) a->n.length--;
}

/* ---- registers ---- */
void apu_write(apu *a, uint16_t addr, uint8_t v) {
    if (addr >= 0x4000 && addr <= 0x4007) {
        apu_pulse *p = &a->p[(addr - 0x4000) >> 2];
        switch (addr & 3) {
        case 0:
            p->duty = v >> 6; p->halt = (v >> 5) & 1; p->env_const = (v >> 4) & 1; p->env_period = v & 15;
            break;
        case 1:
            p->sw_en = v >> 7; p->sw_period = (v >> 4) & 7; p->sw_neg = (v >> 3) & 1; p->sw_shift = v & 7;
            p->sw_reload = 1;
            break;
        case 2:
            p->period = (uint16_t)((p->period & 0x700) | v);
            break;
        case 3:
            p->period = (uint16_t)((p->period & 0xFF) | ((v & 7) << 8));
            if (p->enabled) p->length = length_table[v >> 3];
            p->step = 0;
            p->env_start = 1;
            break;
        }
        return;
    }
    switch (addr) {
    case 0x4008:
        a->t.control = v >> 7; a->t.lin_reload = v & 0x7F;
        break;
    case 0x400A:
        a->t.period = (uint16_t)((a->t.period & 0x700) | v);
        break;
    case 0x400B:
        a->t.period = (uint16_t)((a->t.period & 0xFF) | ((v & 7) << 8));
        if (a->t.enabled) a->t.length = length_table[v >> 3];
        a->t.lin_reload_flag = 1;
        break;
    case 0x400C:
        a->n.halt = (v >> 5) & 1; a->n.env_const = (v >> 4) & 1; a->n.env_period = v & 15;
        break;
    case 0x400E:
        a->n.mode = v >> 7; a->n.period = noise_table[v & 15];
        break;
    case 0x400F:
        if (a->n.enabled) a->n.length = length_table[v >> 3];
        a->n.env_start = 1;
        break;
    case 0x4015:
        a->p[0].enabled = v & 1; a->p[1].enabled = (v >> 1) & 1;
        a->t.enabled = (v >> 2) & 1; a->n.enabled = (v >> 3) & 1;
        if (!a->p[0].enabled) a->p[0].length = 0;
        if (!a->p[1].enabled) a->p[1].length = 0;
        if (!a->t.enabled) a->t.length = 0;
        if (!a->n.enabled) a->n.length = 0;
        break;
    case 0x4017:
        a->five_step = v >> 7;
        a->fc_cycle = 0;
        a->fc_step = 1; a->fc_gap = 0;     /* FCEUX timing: next step is 1 */
        if (a->five_step) { quarter_frame(a); half_frame(a); }
        break;
    }
}

/* ---- one CPU cycle ---- */
/* Hardware sequence (CPU cycles after a $4017 write): 4-step: quarter at
 * 7457, 14913, 22371, 29829, half at 14913 and 29829, period 29830.
 * 5-step: quarter at 7457, 14913, 22371, 37281, half at 14913 and 37281,
 * period 37282. */
static void frame_counter_tick_hw(apu *a) {
    long c = ++a->fc_cycle;
    if (c == 7457 || c == 22371) quarter_frame(a);
    else if (c == 14913) { quarter_frame(a); half_frame(a); }
    else if (!a->five_step && c == 29829) { quarter_frame(a); half_frame(a); }
    else if (a->five_step && c == 37281) { quarter_frame(a); half_frame(a); }
    if (c == (a->five_step ? 37282 : 29830)) a->fc_cycle = 0;
}

/* FCEUX 2.6.6 timing, for comparing with its output only (apucheck
 * --fceux-timing): steps every 7457.5 cycles, quarter+half on steps 0 and
 * 2, quarter on 1 and 3, and in 5-step mode one empty step after step 3,
 * so its 5-step period is 37287.5 cycles instead of 37282. Counted in
 * half cycles. */
static void frame_counter_tick_fceux(apu *a) {
    a->fc_cycle += 2;
    if (a->fc_cycle < 14915) return;
    a->fc_cycle -= 14915;
    if (a->fc_gap) { a->fc_gap = 0; return; }
    int s = a->fc_step;
    quarter_frame(a);
    if (!(s & 1)) half_frame(a);
    if (s == 3 && a->five_step) a->fc_gap = 1;
    a->fc_step = (uint8_t)((s + 1) & 3);
}

static void frame_counter_tick(apu *a) {
    if (a->fceux_timing) frame_counter_tick_fceux(a);
    else frame_counter_tick_hw(a);
}

static double mix(apu *a) {
    int p_out[2];
    for (int i = 0; i < 2; i++) {
        apu_pulse *p = &a->p[i];
        if (!p->length || pulse_muted(p) || !duty_table[p->duty][p->step]) p_out[i] = 0;
        else p_out[i] = p->env_const ? p->env_period : p->env_decay;
    }
    int t_out = tri_table[a->t.step];
    int n_out = (!a->n.length || (a->n.lfsr & 1)) ? 0 : (a->n.env_const ? a->n.env_period : a->n.env_decay);
    if (a->mute & 1) p_out[0] = 0;
    if (a->mute & 2) p_out[1] = 0;
    if (a->mute & 4) t_out = 0;
    if (a->mute & 8) n_out = 0;
    double pulse = (p_out[0] + p_out[1]) ? 95.88 / (8128.0 / (p_out[0] + p_out[1]) + 100.0) : 0.0;
    double tnd_in = t_out / 8227.0 + n_out / 12241.0;
    double tnd = tnd_in > 0 ? 159.79 / (1.0 / tnd_in + 100.0) : 0.0;
    return pulse + tnd;
}

static void cpu_cycle(apu *a) {
    frame_counter_tick(a);
    /* triangle: every CPU cycle; holds its output while silenced, and
     * periods below 2 (ultrasonic) are not stepped */
    apu_triangle *t = &a->t;
    if (t->timer) t->timer--;
    else {
        t->timer = t->period;
        if (t->length && t->lin_counter && t->period >= 2) t->step = (t->step + 1) & 31;
    }
    /* noise: period in CPU cycles */
    apu_noise *n = &a->n;
    if (n->timer) n->timer--;
    else {
        n->timer = n->period ? n->period - 1 : 0;
        uint16_t fb = (n->lfsr ^ (n->lfsr >> (n->mode ? 6 : 1))) & 1;
        n->lfsr = (uint16_t)((n->lfsr >> 1) | (fb << 14));
    }
    /* pulses: every second CPU cycle */
    a->odd ^= 1;
    if (a->odd) {
        for (int i = 0; i < 2; i++) {
            apu_pulse *p = &a->p[i];
            if (p->timer) p->timer--;
            else { p->timer = p->period; p->step = (p->step + 1) & 7; }
        }
    }
}

int apu_run(apu *a, long cycles, float *out, int max) {
    int n = 0;
    for (long i = 0; i < cycles; i++) {
        cpu_cycle(a);
        a->acc += mix(a);
        a->acc_n += 1;
        a->phase += 1;
        if (a->phase >= a->cyc_per_sample) {
            a->phase -= a->cyc_per_sample;
            double x = a->acc / a->acc_n;
            a->acc = a->acc_n = 0;
            if (!a->primed) { a->primed = 1; a->hp_prev_in = x; }   /* no click at power-on */
            double y = a->hp_k * (a->hp_prev_out + x - a->hp_prev_in);
            a->hp_prev_in = x; a->hp_prev_out = y;
            a->lp_prev += a->lp_k * (y - a->lp_prev);
            if (n < max) out[n++] = (float)a->lp_prev;
        }
    }
    return n;
}
