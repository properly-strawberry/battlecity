/* difftest: per-routine differential test of the C reconstruction against
 * the original 6502 code.
 *
 * usage: difftest ROM MOVIE FRAMES ADDR OUTREGS [--cases N] [--fuzz M]
 *                 [--seed S] [--maxsteps K] [--maxwaits W] [--capture-at C] [-v]
 *
 * 1. Capture: play MOVIE on the reference emulator for FRAMES frames and
 *    sample up to N machine states at the moment the CPU is about to
 *    execute ADDR (reservoir sampling over all hits).
 * 2. For each captured state, plus M fuzzed variants of each (random RAM
 *    bytes and registers changed), run
 *      - the original: the reference CPU calls ADDR until it returns;
 *      - the reconstruction: sub_ADDR via mech_dispatch, same RAM,
 *        registers and PPU state;
 *    and compare RAM (except the CPU stack window $0120-$017F), the
 *    registers/flags named in OUTREGS (from docs/contracts.json "out"),
 *    and the full ordered hardware-access log.
 * Frames: as in lockstep, time passes only while the program waits for
 *    vblank. The original waits in two loops: wait_frame ($D8F8, cmp
 *    frame_counter) and wait_vblank_flag ($D5F5 PPUSTATUS read, $D5F8 bpl).
 *    When it is about to spin in one of them (at $D8F8 with the counter
 *    unchanged, or at the bpl after reading the flag clear, so its reads
 *    match the reconstruction's), the harness runs the pre-render line
 *    and the start of vblank, and takes the NMI if PPUCTRL enables it. The
 *    reconstruction's hal_wait_vblank does the same on its device and calls
 *    bc_nmi(). Both sides run with the captured pads. They must consume the
 *    same number of frames (compared), at most W each (--maxwaits).
 * --capture-at C: capture the entry states at address C instead of ADDR
 *    (for routines no movie reaches; C must be entered in the same context).
 * Cases where the original does not return within K instructions between
 *    two waits, or within W waits, are skipped (counted). This also happens
 *    for fuzzed states that make the code loop.
 * A fuzzed state that sends a jump table out of range makes the original
 *    jump into garbage; the reconstruction refuses (bad dispatch). Such
 *    cases are counted as invalid, not as failures, as are fuzzed states
 *    outside a routine's input domain (in_domain()).
 * Exit status: 0 if every executed case matched. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../emu/nes.h"
#include "../../src/machine.h"
#include "../../src/rom.h"
#include "../../src/mech/rt.h"

#define STACK_LO 0x120
#define STACK_HI 0x17F

static nes *dev;
static jmp_buf case_abort;
static int abort_reason;   /* 1 = waited for vblank, 2 = brk/bad dispatch */

uint8_t hal_read(uint16_t a) { return nes_io_read(dev, a); }
void hal_write(uint16_t a, uint8_t v) {
    if (a == 0x4014) memcpy(dev->ram, RAM, 0x800);
    nes_io_write(dev, a, v);
}
static long maxwaits = 4000;
static long rec_waits;

/* growable I/O event log (one routine call can span thousands of frames) */
typedef struct { uint16_t addr; uint8_t v, r; } ev;
typedef struct { ev *v; long n, cap; } evvec;
static evvec ea, eb;
static void drain(nes *n, evvec *o) {
    for (uint32_t i = 0; i < n->iolog_len; i++) {
        if (o->n == o->cap) { o->cap = o->cap ? 2 * o->cap : 4096; o->v = realloc(o->v, o->cap * sizeof *o->v); }
        o->v[o->n].addr = n->iolog[i].addr; o->v[o->n].v = n->iolog[i].value; o->v[o->n].r = n->iolog[i].is_read; o->n++;
    }
    n->iolog_len = 0;
}

/* Reconstruction waits: same frame model as lockstep's hal_wait_vblank. */
void hal_wait_vblank(void) {
    if (++rec_waits > maxwaits) { abort_reason = 1; longjmp(case_abort, 1); }
    drain(dev, &eb);
    nes_prerender(dev);
    nes_vblank_start(dev);
    if (dev->ctrl & 0x80) bc_nmi();
}
void mech_brk(uint16_t at) { (void)at; abort_reason = 2; longjmp(case_abort, 1); }
void mech_bad_dispatch(uint16_t addr) { (void)addr; abort_reason = 2; longjmp(case_abort, 1); }

/* Input domains: fuzzed states outside the values a routine is ever called
 * with are counted as invalid, not tested. Each entry says why. */
static int in_domain(uint16_t addr, const nes_state *st) {
    /* Game-wide invariant: bullet_state ($CC-$D5) < $50. Every writer stores
     * $00, $33, $40|dir, a copy of another slot, or a countdown from $3x
     * ($E076-$E0A0, $E148, $E158, $E40D, $E6C1, $E6D0, $E964). Any routine
     * that draws bullets ($E0D8, and its callers $C41D and the frame loops)
     * would otherwise jump through the 5-entry table at $E4E2 into data. */
    for (int i = 0; i < 10; i++) if (st->ram[0xCC + i] >= 0x50) return 0;
    switch (addr) {
    case 0xF000:   /* load_level: callers ($C1D9, $C3D5) pass stage_number 1..70 or $FF.
                    * Larger A advances the map pointer past $FFFF into RAM, so the
                    * original reads its own stack page as map data. */
        return st->a == 0xFF || (st->a >= 1 && st->a <= 0x46);
    case 0xE0E6:   /* draw_bullet: X = slot 0..9 (only caller $E0DE) */
    case 0xE03C:   /* bullet_dispatch: X = slot 0..9 (only caller update_bullets
                    * $E032-$E039); a larger X reads a byte that is not a
                    * bullet_state (zero-page wrap) */
        return st->x <= 9;
    }
    return 1;
}

/* ---- capture ---- */
static uint16_t target;
static nes_state *caps;
static int ncap, maxcap;
static long hits;
static unsigned long rng_s = 88172645463325252UL;
static unsigned long xr(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return rng_s; }

static uint16_t capture_at;
static void hook(nes *n, uint16_t pc) {
    if (pc != capture_at) return;
    hits++;
    if (ncap < maxcap) nes_save_state(n, &caps[ncap++]);
    else {
        long j = (long)(xr() % (unsigned long)hits);
        if (j < maxcap) nes_save_state(n, &caps[j]);
    }
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

/* Call the original at addr until it returns (RTS to the $FFFF sentinel).
 * Frames pass at its two wait loops (see the header). Returns 0 ok,
 * 1 = more than max_steps instructions between waits or more than maxwaits
 * waits, 2 = CPU halted. *waits = frames consumed. */
static int ref_call(nes *n, uint16_t addr, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *p,
                    long max_steps, long *waits) {
    cpu6502 *c = &n->cpu;
    c->halted = 0;
    c->a = *a; c->x = *x; c->y = *y; c->p = *p | 0x20;
    uint8_t sp0 = c->sp;
    c->write(n, 0x100 | c->sp, 0xFF); c->sp--;
    c->write(n, 0x100 | c->sp, 0xFF); c->sp--;
    c->pc = addr;
    long steps = 0;
    int rc = 0;
    *waits = 0;
    n->nmi_pending = 0;
    uint16_t waited_at = 0;   /* run the waiting instruction once after its frame */
    for (;;) {
        if (c->pc == 0x0000 && c->sp == sp0) break;
        if (c->pc == waited_at) waited_at = 0;
        else if ((c->pc == 0xD8F8 && c->a == n->ram[0x0B]) ||     /* wait_frame would spin */
            (c->pc == 0xD5F8 && !(c->p & 0x80))) {           /* wait_vblank_flag: bpl after a clear read */
            if (++*waits > maxwaits) { rc = 1; break; }
            drain(n, &ea);
            nes_prerender(n);
            nes_vblank_start(n);
            waited_at = c->pc;
            if (n->ctrl & 0x80) cpu_nmi(c);
            steps = 0;
            continue;
        }
        if (n->nmi_pending) { n->nmi_pending = 0; cpu_nmi(c); continue; }   /* NMI enabled during vblank */
        if (steps++ >= max_steps) { rc = 1; break; }
        cpu_step(c);
        if (c->halted) { rc = 2; break; }
    }
    drain(n, &ea);
    *a = c->a; *x = c->x; *y = c->y; *p = c->p;
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: difftest ROM MOVIE FRAMES ADDR OUTREGS [--cases N] [--fuzz M] [--seed S] [--maxsteps K] [--maxwaits W] [--capture-at C] [-v]\n"); return 2; }
    int frames = atoi(argv[3]);
    target = (uint16_t)strtol(argv[4], NULL, 16);
    const char *outregs = argv[5];
    int fuzz = 3, verbose = 0; long maxsteps = 2000000;
    maxcap = 200;
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--cases")) maxcap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fuzz")) fuzz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed")) rng_s ^= strtoul(argv[++i], NULL, 0) * 2654435761UL;
        else if (!strcmp(argv[i], "--maxsteps")) maxsteps = atol(argv[++i]);
        else if (!strcmp(argv[i], "--maxwaits")) maxwaits = atol(argv[++i]);
        else if (!strcmp(argv[i], "--capture-at")) capture_at = (uint16_t)strtol(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
    }
    if (!capture_at) capture_at = target;
    static bc_rom_info rom;
    char err[512];
    if (bc_rom_load(argv[1], &rom, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    uint8_t *p1 = calloc(frames + 1, 1), *p2 = calloc(frames + 1, 1);
    if (strcmp(argv[2], "-")) {
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
    }
    caps = calloc(maxcap, sizeof *caps);
    nes *ref = nes_new(PRG, rom.chr);
    ref->horizontal = !(rom.flags6 & 1);
    ref->iolog_enabled = 0;
    ref->exec_hook = hook;
    nes_power(ref);
    for (int fr = 0; fr < frames; fr++) nes_run_frame(ref, p1[fr], p2[fr]);
    ref->exec_hook = NULL;
    if (!ncap) { printf("%04X: never reached in %d frames\n", target, frames); return 3; }

    nes *r = nes_new(PRG, rom.chr);
    dev = nes_new(PRG, rom.chr);
    r->horizontal = dev->horizontal = !(rom.flags6 & 1);
    r->iolog_enabled = dev->iolog_enabled = 1;
    long pass = 0, fail = 0, skipped_ref = 0, waited = 0, invalid = 0;
    for (int c = 0; c < ncap; c++) {
        for (int v = 0; v <= fuzz; v++) {
            nes_state st = caps[c];
            char fz[160] = "";   /* fuzzed RAM bytes, for FAIL reports */
            if (v > 0) {
                int k = 1 + (int)(xr() % 6);
                for (int j = 0; j < k; j++) {
                    int a = (int)(xr() % 0x800);
                    if (a >= STACK_LO && a <= STACK_HI) continue;
                    st.ram[a] = (uint8_t)xr();
                    size_t l = strlen(fz);
                    snprintf(fz + l, sizeof fz - l, " $%04X=%02X", a, st.ram[a]);
                }
                if (xr() & 1) st.a = (uint8_t)xr();
                if (xr() & 1) st.x = (uint8_t)xr();
                if (xr() & 1) st.y = (uint8_t)xr();
                if (xr() & 1) st.p = (uint8_t)((st.p & 0x34) | (xr() & 0xC3));
                if (!in_domain(target, &st)) { invalid++; continue; }
            }
            /* original */
            nes_load_state(r, &st);
            r->iolog_len = 0;
            ea.n = 0;
            /* static: they must survive the reconstruction's longjmp */
            static uint8_t ra, rx, ry, rp;
            static long ref_waits;
            ra = st.a; rx = st.x; ry = st.y; rp = st.p;
            int rc = ref_call(r, target, &ra, &rx, &ry, &rp, maxsteps, &ref_waits);
            if (rc) { skipped_ref++; continue; }
            /* reconstruction */
            nes_load_state(dev, &st);
            dev->iolog_len = 0;
            eb.n = 0;
            rec_waits = 0;
            memcpy(RAM, st.ram, 0x800);
            A = st.a; X = st.x; Y = st.y; SETP(st.p); S = st.sp;
            if (setjmp(case_abort)) {
                if (abort_reason == 1) {
                    fail++;
                    if (verbose || fail <= 5) printf("case %d/%d: reconstruction still waiting after %ld frames (original returned after %ld)\n", c, v, maxwaits, ref_waits);
                    continue;
                }
                if (v > 0) { invalid++; continue; }   /* fuzzed state drove a jump table out of range */
                fail++;
                if (verbose || fail <= 5) printf("case %d/%d: reconstruction hit BRK/bad dispatch\n", c, v);
                continue;
            }
            PUSH(0xFF); PUSH(0xFF);
            mech_dispatch(target);
            drain(dev, &eb);
            if (ref_waits) waited++;
            /* compare */
            int bad = 0;
            char why[512] = "";
            for (int a = 0; a < 0x800 && !bad; a++) {
                if (a >= STACK_LO && a <= STACK_HI) continue;
                if (r->ram[a] != RAM[a]) { bad = 1; snprintf(why, sizeof why, "RAM[$%04X] orig=%02X rec=%02X", a, r->ram[a], RAM[a]); }
            }
            for (const char *q = outregs; *q && !bad; q++) {
                uint8_t o = 0, n = 0;
                switch (*q) {
                case 'A': o = ra; n = A; break;
                case 'X': o = rx; n = X; break;
                case 'Y': o = ry; n = Y; break;
                case 'C': o = rp & 1; n = fC; break;
                case 'Z': o = (rp >> 1) & 1; n = fZ; break;
                case 'V': o = (rp >> 6) & 1; n = fV; break;
                case 'N': o = rp >> 7; n = fN; break;
                }
                if (o != n) { bad = 1; snprintf(why, sizeof why, "reg %c orig=%02X rec=%02X", *q, o, n); }
            }
            if (!bad && ref_waits != rec_waits) { bad = 1; snprintf(why, sizeof why, "frames orig=%ld rec=%ld", ref_waits, rec_waits); }
            if (!bad) {
                long m = ea.n < eb.n ? ea.n : eb.n;
                for (long i = 0; i < m && !bad; i++)
                    if (ea.v[i].addr != eb.v[i].addr || ea.v[i].v != eb.v[i].v || ea.v[i].r != eb.v[i].r) {
                        bad = 1;
                        snprintf(why, sizeof why, "io #%ld orig=%c%04X:%02X rec=%c%04X:%02X", i,
                                 ea.v[i].r ? 'R' : 'W', ea.v[i].addr, ea.v[i].v, eb.v[i].r ? 'R' : 'W', eb.v[i].addr, eb.v[i].v);
                    }
                if (!bad && ea.n != eb.n) { bad = 1; snprintf(why, sizeof why, "io count orig=%ld rec=%ld", ea.n, eb.n); }
            }
            if (bad) {
                fail++;
                if (verbose || fail <= 5)
                    printf("FAIL case %d%s (A=%02X X=%02X Y=%02X P=%02X%s%s): %s\n", c, v ? " fuzzed" : "", st.a, st.x, st.y, st.p,
                           *fz ? ", RAM" : "", fz, why);
            } else pass++;
        }
    }
    printf("%04X: %ld pass, %ld fail, %ld skipped (original did not return), %ld waited, %ld invalid fuzz (jump table out of range or outside the input domain); %ld entries seen, %d sampled\n",
           target, pass, fail, skipped_ref, waited, invalid, hits, ncap);
    return fail ? 1 : 0;
}
