/* lockstep: run the original ROM (reference emulator) and the C
 * reconstruction side by side and compare them at every NMI.
 *
 * usage: lockstep ROM.nes MOVIE NMIS [-v] [--raminit N]
 *
 * --raminit: power-on RAM for BOTH sides; 0 = FCEUX default pattern
 *   (00 00 00 00 FF FF FF FF ...), the default so that FCEUX recordings
 *   replay; 2 = zeros. The game reads uninitialised RAM (RNG, frame
 *   counters), so this choice changes the whole run.
 *
 * MOVIE uses the nesrun format and is indexed by video frame, exactly like
 * nesrun and fceux_headless, so FCEUX recordings replay unchanged. The
 * reference consumes it frame by frame. The reconstruction has no frames
 * of its own, so its NMI k sees the pads of the frame in which the
 * reference took NMI k. (The game latches pads inside the NMI handler.)
 * NMIS counts sync points; movies shorter than the run read as no input.
 *
 * Sync point = NMI entry. At NMI k both sides are compared on:
 *   - CPU RAM $0000-$07FF, except the CPU stack window $0120-$017F.
 *     That window holds only return addresses and saved registers (deepest
 *     measured SP = $6B; page-1 variables are $0100-$0108, the boot
 *     signature $0110-$011F and the VRAM queue $0180-$01FF). Lifted C code
 *     does not reproduce 6502 return addresses, so the window is not part
 *     of the observable state.
 *   - the ordered list of hardware register accesses since NMI k-1:
 *     all writes $2000-$4017, and reads of $2007/$4015/$4016/$4017
 *     ($2002 reads are excluded: the original busy-polls them and the
 *     reconstruction polls once per wait; their side effects are covered).
 * Frames in which the original runs with NMI disabled are not sync
 * points. The reconstruction finishes such work without consuming frames.
 * The time it takes is not modelled (see docs/verification.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../emu/nes.h"
#include "../../src/machine.h"
#include "../../src/rom.h"
#include <signal.h>
#include <unistd.h>
#ifdef MECH_TRACE
volatile uint16_t mech_last_pc;
static void on_alarm(int sig) { (void)sig; char b[64]; int n = snprintf(b, sizeof b, "TIMEOUT: rec PC %04X\n", mech_last_pc); if (write(1, b, n) < 0) {} _exit(4); }
#endif

#define STACK_LO 0x120
#define STACK_HI 0x17F
static nes *ref;      /* original ROM on the reference CPU */
static nes *dev;      /* PPU/APU/pad device used by the reconstruction */
static uint8_t *movie, *movie2;
static long nmis_wanted;
static long nmi_index;   /* reconstruction-side NMI count */
static int verbose;

typedef struct { io_event *v; size_t n, cap; } evvec;
static evvec ref_ev, dev_ev;
static size_t ref_seg_start, dev_seg_start;

static int relevant(const io_event *e) {
    if (e->addr == 0x2002 && e->is_read) return 0;
    return 1;
}
static void evpush(evvec *ev, const io_event *e) {
    if (!relevant(e)) return;
    if (ev->n == ev->cap) { ev->cap = ev->cap ? ev->cap * 2 : 4096; ev->v = realloc(ev->v, ev->cap * sizeof *ev->v); }
    ev->v[ev->n++] = *e;
}
static void drain(nes *n, evvec *ev, uint32_t upto) {
    for (uint32_t i = 0; i < upto; i++) evpush(ev, &n->iolog[i]);
}

static long movie_len;
static long ref_nmi_frame;   /* frame in which the reference took its last NMI */
static uint8_t movie_pad(uint8_t *m, long k) { return k < movie_len ? m[k] : 0; }

/* advance the reference until it takes its next NMI; returns ref RAM snap */
static void ref_next_nmi(void) {
    uint32_t want = ref->nmi_count + 1;
    long guard = 0;
    while (ref->nmi_count < want) {
        ref->iolog_len = 0;
        ref->nmi_snap_valid = 0;
        long fr = ref->frame;
        nes_run_frame(ref, movie_pad(movie, fr), movie_pad(movie2, fr));
        if (ref->cpu.halted) { printf("REF CPU halted at %04X\n", ref->cpu.halt_pc); exit(3); }
        if (ref->nmi_snap_valid) {
            ref_nmi_frame = fr;
            drain(ref, &ref_ev, ref->nmi_iolog_pos);
            /* the rest of this frame's events belong to the next segment;
             * keep them aside */
            static io_event tail[IOLOG_MAX];
            uint32_t nt = ref->iolog_len - ref->nmi_iolog_pos;
            memcpy(tail, ref->iolog + ref->nmi_iolog_pos, nt * sizeof *tail);
            ref->iolog_len = 0;
            /* stash tail in ref->iolog for the next call */
            memcpy(ref->iolog, tail, nt * sizeof *tail);
            ref->iolog_len = nt;
            ref->nmi_iolog_pos = 0;
            return;
        }
        drain(ref, &ref_ev, ref->iolog_len);
        if (++guard > 600) { printf("REF: no NMI within 600 frames\n"); exit(3); }
    }
}

static void report_and_exit(int rc) {
    printf("%s after %ld NMIs\n", rc ? "DIVERGED" : "OK: identical", nmi_index);
    exit(rc);
}

static void compare_at_nmi(void) {
    /* keep leftover events from the previous call */
    {
        uint32_t keep = ref->iolog_len;
        for (uint32_t i = 0; i < keep; i++) evpush(&ref_ev, &ref->iolog[i]);
        ref->iolog_len = 0;
    }
    ref_next_nmi();
    int bad = 0;
    /* RAM */
    int nd = 0;
    for (int a = 0; a < 0x800; a++) {
        if (a >= STACK_LO && a <= STACK_HI) continue;   /* CPU stack window */
        if (ref->nmi_ram[a] != RAM[a]) {
            if (nd < 24) printf("NMI %ld: RAM[$%04X] ref=%02X rec=%02X\n", nmi_index, a, ref->nmi_ram[a], RAM[a]);
            nd++;
        }
    }
    if (nd) { printf("NMI %ld: %d RAM bytes differ\n", nmi_index, nd); bad = 1; }
    /* io segment */
    size_t rn = ref_ev.n - ref_seg_start, dn = dev_ev.n - dev_seg_start;
    size_t m = rn < dn ? rn : dn;
    for (size_t i = 0; i < m; i++) {
        io_event *a = &ref_ev.v[ref_seg_start + i], *b = &dev_ev.v[dev_seg_start + i];
        if (a->addr != b->addr || a->value != b->value || a->is_read != b->is_read) {
            printf("NMI %ld: io #%zu ref=%c%04X:%02X rec=%c%04X:%02X\n", nmi_index, i,
                   a->is_read ? 'R' : 'W', a->addr, a->value, b->is_read ? 'R' : 'W', b->addr, b->value);
            bad = 1;
            break;
        }
    }
    if (rn != dn) { printf("NMI %ld: io count ref=%zu rec=%zu\n", nmi_index, rn, dn); bad = 1; }
    if (verbose) {
        printf("NMI %ld (%zu io events) ref pc=%04X sp=%02X stack:", nmi_index, rn, ref->nmi_pc_last, ref->nmi_sp);
        for (int a = ref->nmi_sp + 1; a < 0x100; a++) printf(" %02X", ref->nmi_ram[0x100 + a]);
        printf(" | rec sp=%02X stack:", S);
        for (int a = S + 1; a < 0x100; a++) printf(" %02X", RAM[0x100 + a]);
        printf("\n");
    }
    ref_seg_start = ref_ev.n;
    dev_seg_start = dev_ev.n;
    if (bad) report_and_exit(1);
}

/* ---- HAL for the reconstruction ---- */
uint8_t hal_read(uint16_t a) {
    uint8_t v = nes_io_read(dev, a);
    for (uint32_t i = 0; i < dev->iolog_len; i++) evpush(&dev_ev, &dev->iolog[i]);
    dev->iolog_len = 0;
    return v;
}
void hal_write(uint16_t a, uint8_t v) {
    if (a == 0x4014) memcpy(dev->ram, RAM, 0x800);   /* OAM DMA source */
    nes_io_write(dev, a, v);
    for (uint32_t i = 0; i < dev->iolog_len; i++) evpush(&dev_ev, &dev->iolog[i]);
    dev->iolog_len = 0;
}
void hal_wait_vblank(void) {
    /* Time passes only while the program waits: the pre-render line of the
     * previous frame (vblank flag cleared, render state latched) and the
     * new vblank both happen here. The flag stays set afterwards, as during
     * the real 20-line vblank, so PPUSTATUS polls see it. */
    nes_prerender(dev);
    nes_vblank_start(dev);
    if (dev->ctrl & 0x80) {
        nmi_index++;
        compare_at_nmi();
        if (nmi_index >= nmis_wanted) report_and_exit(0);
        dev->pad[0] = movie_pad(movie, ref_nmi_frame);
        dev->pad[1] = movie_pad(movie2, ref_nmi_frame);
        bc_nmi();
    }
}

void mech_brk(uint16_t at) { printf("REC: BRK at %04X\n", at); report_and_exit(2); }
void mech_bad_dispatch(uint16_t addr) { printf("REC: bad dispatch to %04X\n", addr); report_and_exit(2); }

/* ---- movie ---- */
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
static void load_movie(const char *path, long n) {
    movie = calloc(n + 1, 1); movie2 = calloc(n + 1, 1);
    movie_len = n;
    if (!strcmp(path, "-")) return;
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char line[256]; long pos = 0;
    while (fgets(line, sizeof line, f) && pos < n) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int cnt; char b1[64] = ".", b2[64] = ".";
        if (sscanf(line, "%d %63s %63s", &cnt, b1, b2) < 1) continue;
        for (int i = 0; i < cnt && pos < n; i++) { movie[pos] = parse_buttons(b1); movie2[pos++] = parse_buttons(b2); }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: lockstep ROM MOVIE NMIS [-v]\n"); return 2; }
    static bc_rom_info rom;
    char err[512];
    if (bc_rom_load(argv[1], &rom, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    nmis_wanted = atol(argv[3]);
    int raminit = 0;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--raminit")) raminit = atoi(argv[++i]);
    }
    load_movie(argv[2], nmis_wanted * 4);   /* frames >= NMIs; generous */
    ref = nes_new(PRG, rom.chr);
    dev = nes_new(PRG, rom.chr);
    /* iNES flags6 bit 0 = 0: horizontal mirroring */
    ref->horizontal = dev->horizontal = !(rom.flags6 & 1);
    ref->iolog_enabled = dev->iolog_enabled = 1;
    ref->ram_init = dev->ram_init = raminit;
    nes_power(ref);
    nes_power(dev);
    memcpy(RAM, ref->ram, 0x800);   /* same power-on RAM for the reconstruction */
#ifdef MECH_TRACE
    signal(SIGALRM, on_alarm); alarm(5);
#endif
    bc_reset();
    printf("reconstruction returned from reset?\n");
    return 2;
}
