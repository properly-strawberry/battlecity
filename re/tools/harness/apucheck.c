/* apucheck: render the original's sound with our APU (src/platform/apu.c).
 *
 * usage: apucheck ROM MOVIE FRAMES PREFIX [--trace F0 F1] [--state F0 F1] [--offsets] [--fceux-timing] [--mute MASK]
 *   --trace: print the channel state at the end of frames F0..F1.
 *   --fceux-timing: use FCEUX's frame-counter step timing (see apu.c), to
 *     compare with fceux_headless --audio without its timing drift.
 *   --state F0 F1: print one line per frame F0..F1 for tools/apu_state_compare.py:
 *     frame p1per p1len p1env p2per p2len p2env tlen tlin nlen nenv
 *   --offsets: print "frame cycle_in_frame addr value" for every APU write
 *     (cycle 0 = start of the frame chunk, the end of the previous line 239).
 *   --mute MASK: silence channels (bit 0/1 pulse 1/2, 2 triangle, 3 noise).
 *
 * Plays MOVIE on the reference emulator; every $4000-$4017 write reaches the
 * APU at the CPU cycle it happens (to within one instruction). Writes
 * PREFIX.f32 (mono float, 48 kHz) and PREFIX.cnt (samples per frame), the
 * same format as `fceux_headless --audio`, so tools/audio_compare.py can
 * compare the two frame by frame. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../emu/nes.h"
#include "../../src/machine.h"
#include "../../src/rom.h"
#include "../../src/platform/apu.h"

static apu snd;
static uint64_t apu_time;    /* CPU cycle the APU has reached */
static float buf[4096];
static int nbuf;

static void advance_to(uint64_t t) {
    if (t > apu_time) {
        nbuf += apu_run(&snd, (long)(t - apu_time), buf + nbuf, (int)(sizeof buf / sizeof *buf) - nbuf);
        apu_time = t;
    }
}
static int print_offsets;
static uint64_t frame_start;
static int cur_frame;
static void on_apu_write(nes *n, uint16_t a, uint8_t v) {
    if (print_offsets) printf("%d %llu %04X %02X\n", cur_frame, (unsigned long long)(n->cpu.cycles - frame_start), a, v);
    advance_to(n->cpu.cycles);
    apu_write(&snd, a, v);
}

static uint8_t parse_buttons(const char *s) {
    uint8_t b = 0;
    for (; *s; s++) switch (*s) {
        case 'A': b |= 0x01; break; case 'B': b |= 0x02; break;
        case 's': b |= 0x04; break; case 'S': b |= 0x08; break;
        case 'U': b |= 0x10; break; case 'D': b |= 0x20; break;
        case 'L': b |= 0x40; break; case 'R': b |= 0x80; break;
    }
    return b;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: apucheck ROM MOVIE FRAMES PREFIX [--trace F0 F1]\n"); return 2; }
    long tr0 = -1, tr1 = -1, st0 = -1, st1 = -1;
    int fceux_timing = 0, mute = 0;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--trace") && i + 2 < argc) { tr0 = atol(argv[i + 1]); tr1 = atol(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--fceux-timing")) fceux_timing = 1;
        else if (!strcmp(argv[i], "--offsets")) print_offsets = 1;
        else if (!strcmp(argv[i], "--state") && i + 2 < argc) { st0 = atol(argv[i + 1]); st1 = atol(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--mute") && i + 1 < argc) mute = (int)strtol(argv[++i], NULL, 0);
    }
    static bc_rom_info rom;
    char err[512];
    if (bc_rom_load(argv[1], &rom, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    int frames = atoi(argv[3]);
    uint8_t *p1 = calloc(frames + 1, 1), *p2 = calloc(frames + 1, 1);
    FILE *m = fopen(argv[2], "r");
    if (!m) { perror(argv[2]); return 2; }
    char line[256]; int pos = 0;
    while (fgets(line, sizeof line, m) && pos < frames) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int cnt; char b1[64] = ".", b2[64] = ".";
        if (sscanf(line, "%d %63s %63s", &cnt, b1, b2) < 1) continue;
        for (int k = 0; k < cnt && pos < frames; k++) { p1[pos] = parse_buttons(b1); p2[pos++] = parse_buttons(b2); }
    }
    fclose(m);

    char path[1024];
    snprintf(path, sizeof path, "%s.f32", argv[4]);
    FILE *af = fopen(path, "wb");
    snprintf(path, sizeof path, "%s.cnt", argv[4]);
    FILE *cf = fopen(path, "w");
    if (!af || !cf) { perror(argv[4]); return 2; }

    apu_init(&snd, 48000);
    snd.fceux_timing = (uint8_t)fceux_timing;
    snd.mute = (uint8_t)mute;
    nes *n = nes_new(PRG, rom.chr);
    n->horizontal = !(rom.flags6 & 1);
    n->apu_hook = on_apu_write;
    nes_power(n);
    apu_time = n->cpu.cycles;
    for (int fr = 0; fr < frames; fr++) {
        nbuf = 0;
        frame_start = n->cpu.cycles;
        cur_frame = fr;
        nes_run_frame(n, p1[fr], p2[fr]);
        advance_to(n->cpu.cycles);
        fwrite(buf, sizeof *buf, nbuf, af);
        if (fr >= st0 && fr <= st1)
            printf("%d %d %d %d %d %d %d %d %d %d %d\n", fr,
                   snd.p[0].period, snd.p[0].length, snd.p[0].env_decay,
                   snd.p[1].period, snd.p[1].length, snd.p[1].env_decay,
                   snd.t.length, snd.t.lin_counter, snd.n.length, snd.n.env_decay);
        if (fr >= tr0 && fr <= tr1) {
            for (int c = 0; c < 2; c++) {
                apu_pulse *p = &snd.p[c];
                printf("%d P%d per=%03X len=%3d env=%2d%s duty=%d sweep=%d/%d/%d/%d div=%d%s\n", fr, c + 1, p->period,
                       p->length, p->env_const ? p->env_period : p->env_decay, p->env_const ? "c" : "",
                       p->duty, p->sw_en, p->sw_period, p->sw_neg, p->sw_shift, p->sw_div,
                       (p->period < 8 || (!p->sw_neg && p->period + (p->period >> p->sw_shift) > 0x7FF)) ? " MUTED" : "");
            }
            printf("%d T per=%03X len=%d lin=%d  N per=%d len=%d env=%d fc=%ld\n", fr, snd.t.period, snd.t.length,
                   snd.t.lin_counter, snd.n.period, snd.n.length, snd.n.env_const ? snd.n.env_period : snd.n.env_decay, snd.fc_cycle);
        }
        fprintf(cf, "%d\n", nbuf);
    }
    fclose(af);
    fclose(cf);
    return 0;
}
