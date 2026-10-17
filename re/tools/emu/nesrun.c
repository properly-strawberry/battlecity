/* nesrun: run the ROM in the reference harness with a scripted input.
 *
 * usage: nesrun ROM.nes MOVIE FRAMES [--ram OUT.bin] [--io OUT.txt]
 *               [--shot FRAME:OUT.ppm]... [--cov OUT.bin]
 *
 * MOVIE: text, one "COUNT BUTTONS [BUTTONS2]" per line ('#' comments). BUTTONS are
 * letters from "ABsSUDLR" (s=Select, S=Start) or '.' for none; held for
 * COUNT frames. Input for frame N is latched before frame N runs.
 * --mirror H|V overrides the header's nametable mirroring.
 * --nmipc writes the PC interrupted by each frame's NMI.
 * --raminit N  power-on RAM: 0 = FCEUX default pattern (default), 2 = zeros.
 * --uninit F    list RAM bytes read before any write, with the first reader PC.
 * --state writes final PPU state (vram, palette, oam, ctrl, mask, t, fine_x).
 * --ram writes 2048 bytes of CPU RAM per frame, sampled at the end of each
 * frame (= start of the next vblank, before NMI). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

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

static uint8_t *load_movie(const char *path, int frames, uint8_t **in2p) {
    uint8_t *in = calloc(frames + 1, 1);
    uint8_t *in2 = calloc(frames + 1, 1);
    *in2p = in2;
    if (strcmp(path, "-") == 0) return in;
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[256]; int pos = 0;
    while (fgets(line, sizeof line, f) && pos < frames) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int cnt; char btn[64] = ".", btn2[64] = ".";
        if (sscanf(line, "%d %63s %63s", &cnt, btn, btn2) < 1) continue;
        uint8_t b = parse_buttons(btn), b2 = parse_buttons(btn2);
        for (int i = 0; i < cnt && pos < frames; i++) { in2[pos] = b2; in[pos++] = b; }
    }
    fclose(f);
    return in;
}

static void write_ppm(nes *n, const char *path) {
    static uint8_t rgb[256 * 240 * 3];
    nes_render(n, rgb);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n256 240\n255\n");
    fwrite(rgb, 1, sizeof rgb, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: nesrun ROM MOVIE FRAMES [opts]\n"); return 2; }
    FILE *rf = fopen(argv[1], "rb");
    if (!rf) { perror(argv[1]); return 1; }
    static uint8_t rom[16 + 0x4000 + 0x2000];
    if (fread(rom, 1, sizeof rom, rf) != sizeof rom) { fprintf(stderr, "short rom\n"); return 1; }
    fclose(rf);
    int frames = atoi(argv[3]);
    uint8_t *in2;
    uint8_t *in = load_movie(argv[2], frames, &in2);
    const char *ram_out = NULL, *io_out = NULL, *cov_out = NULL, *state_out = NULL, *nmipc_out = NULL;
    char mirror_override = 0;
    int raminit = 0;
    const char *uninit_out = NULL;
    int shot_frame[64]; const char *shot_path[64]; int nshots = 0;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--ram")) ram_out = argv[++i];
        else if (!strcmp(argv[i], "--io")) io_out = argv[++i];
        else if (!strcmp(argv[i], "--cov")) cov_out = argv[++i];
        else if (!strcmp(argv[i], "--state")) state_out = argv[++i];
        else if (!strcmp(argv[i], "--nmipc")) nmipc_out = argv[++i];
        else if (!strcmp(argv[i], "--mirror")) mirror_override = argv[++i][0];
        else if (!strcmp(argv[i], "--raminit")) raminit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--uninit")) uninit_out = argv[++i];
        else if (!strcmp(argv[i], "--shot")) {
            char *s = argv[++i]; char *c = strchr(s, ':');
            *c = 0; shot_frame[nshots] = atoi(s); shot_path[nshots++] = c + 1;
        }
    }
    nes *n = nes_new(rom + 16, rom + 16 + 0x4000);
    n->horizontal = !(rom[6] & 1);
    if (mirror_override == 'H') n->horizontal = 1;
    if (mirror_override == 'V') n->horizontal = 0;
    n->ram_init = raminit;
    n->cov_enabled = cov_out != NULL;
    n->iolog_enabled = io_out != NULL;
    nes_power(n);
    FILE *ramf = ram_out ? fopen(ram_out, "wb") : NULL;
    FILE *iof = io_out ? fopen(io_out, "w") : NULL;
    FILE *nmif = nmipc_out ? fopen(nmipc_out, "w") : NULL;
    for (int fr = 0; fr < frames; fr++) {
        n->iolog_len = 0;
        n->nmi_pc_last = 0;
        nes_run_frame(n, in[fr], in2[fr]);
        if (nmif) fprintf(nmif, "%d %04X\n", fr, n->nmi_pc_last);
        if (n->cpu.halted) {
            fprintf(stderr, "CPU halted at %04X op %02X (frame %d)\n", n->cpu.halt_pc, n->cpu.halt_op, fr);
            return 1;
        }
        /* nes_run_frame returns just after the pre-render line; RAM sample
         * point for cross-checks is that moment. */
        if (ramf) fwrite(n->ram, 1, 0x800, ramf);
        if (iof) {
            for (uint32_t i = 0; i < n->iolog_len; i++) {
                io_event *e = &n->iolog[i];
                fprintf(iof, "%u %c %04X %02X\n", e->frame, e->is_read ? 'R' : 'W', e->addr, e->value);
            }
            if (n->iolog_overflow) { fprintf(stderr, "iolog overflow frame %d\n", fr); n->iolog_overflow = 0; }
        }
        for (int s = 0; s < nshots; s++) if (shot_frame[s] == fr) write_ppm(n, shot_path[s]);
    }
    if (ramf) fclose(ramf);
    if (nmif) fclose(nmif);
    if (iof) fclose(iof);
    if (uninit_out) {
        FILE *f = fopen(uninit_out, "w");
        for (int a = 0; a < 0x800; a++)
            if (n->uninit_pc[a]) fprintf(f, "%04X %04X\n", a, n->uninit_pc[a]);
        fclose(f);
    }
    if (state_out) {
        /* final PPU state: vram 2048, palette 32, oam 256, ctrl, mask, t lo, t hi, fine_x */
        FILE *f = fopen(state_out, "wb");
        fwrite(n->vram, 1, 0x800, f); fwrite(n->palette, 1, 32, f); fwrite(n->oam, 1, 256, f);
        uint8_t r[5] = { n->ctrl, n->mask, (uint8_t)n->t, (uint8_t)(n->t >> 8), n->fine_x };
        fwrite(r, 1, 5, f); fclose(f);
    }
    if (cov_out) { FILE *f = fopen(cov_out, "wb"); fwrite(n->cov, 1, 0x4000, f); fclose(f); }
    fprintf(stderr, "min SP %02X (at PC %04X)\n", n->min_sp, n->min_sp_pc);
    nes_free(n);
    return 0;
}
