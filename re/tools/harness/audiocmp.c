/* audiocmp: compare two per-frame audio dumps (PREFIX.f32 + PREFIX.cnt, as
 * written by fceux_headless --audio, apucheck and the game's --audio).
 *
 * usage: audiocmp REF_PREFIX OUR_PREFIX [--from F] [--frames N] [--lag L] [--map MAPFILE] [--invert]
 *
 * Per frame: remove each window's mean, then take the best normalised
 * cross-correlation over lags -L..L samples (default 64, 1.3 ms), each
 * over the samples both windows share (so L may be up to half a frame). The two
 * outputs use different output filters, so a perfect match is not 1.0;
 * a wrong pitch, duty, volume step or timing shows up as low correlation.
 * Frames where either side is silent (RMS < 5e-3, about -46 dBFS) are counted separately:
 * one side silent and the other not is a mismatch.
 * --dump: print "frame corr rms_ref rms_our" for every compared frame.
 * --invert: negate OUR samples (the two outputs may differ in polarity).
 * --map: OUR frame k is compared with REF frame map[k] (one number per
 * line), for the game build whose frames are NMIs.
 * Prints a summary and the worst runs; exit 1 if fewer than 99% of the
 * sounding frames correlate >= 0.9. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float *s; long n; long *off; int *cnt; long frames; } dump;

static void load(const char *prefix, dump *d) {
    char p[1024];
    snprintf(p, sizeof p, "%s.f32", prefix);
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); d->n = ftell(f) / 4; fseek(f, 0, SEEK_SET);
    d->s = malloc(d->n * 4 + 4);
    if (fread(d->s, 4, d->n, f) != (size_t)d->n) { fprintf(stderr, "short read %s\n", p); exit(2); }
    fclose(f);
    snprintf(p, sizeof p, "%s.cnt", prefix);
    f = fopen(p, "r");
    if (!f) { perror(p); exit(2); }
    long cap = 1024; d->off = malloc(cap * sizeof *d->off); d->cnt = malloc(cap * sizeof *d->cnt);
    long o = 0; int c;
    while (fscanf(f, "%d", &c) == 1) {
        if (d->frames == cap) { cap *= 2; d->off = realloc(d->off, cap * sizeof *d->off); d->cnt = realloc(d->cnt, cap * sizeof *d->cnt); }
        d->off[d->frames] = o; d->cnt[d->frames++] = c; o += c;
    }
    fclose(f);
}

static double rms(const float *x, int n, double *mean) {
    double m = 0; for (int i = 0; i < n; i++) m += x[i]; m /= n;
    double e = 0; for (int i = 0; i < n; i++) e += (x[i] - m) * (x[i] - m);
    *mean = m;
    return sqrt(e / n);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: audiocmp REF OUR [--from F] [--frames N] [--lag L] [--map FILE]\n"); return 2; }
    long from = 0, nframes = -1; int L = 64;
    const char *mapf = NULL;
    int invert = 0, dumpf = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--from")) from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--frames")) nframes = atol(argv[++i]);
        else if (!strcmp(argv[i], "--lag")) L = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--map")) mapf = argv[++i];
        else if (!strcmp(argv[i], "--invert")) invert = 1;
        else if (!strcmp(argv[i], "--dump")) dumpf = 1;
    }
    dump r = {0}, o = {0};
    load(argv[1], &r); load(argv[2], &o);
    if (invert) for (long i = 0; i < o.n; i++) o.s[i] = -o.s[i];
    long *map = NULL, nmap = 0;
    if (mapf) {
        FILE *f = fopen(mapf, "r");
        if (!f) { perror(mapf); return 2; }
        long cap = 1024, v; map = malloc(cap * sizeof *map);
        while (fscanf(f, "%ld", &v) == 1) { if (nmap == cap) { cap *= 2; map = realloc(map, cap * sizeof *map); } map[nmap++] = v; }
        fclose(f);
    }
    long last = mapf ? (o.frames < nmap ? o.frames : nmap) : (o.frames < r.frames ? o.frames : r.frames);
    if (nframes >= 0 && from + nframes < last) last = from + nframes;

    long sounding = 0, good = 0, both_silent = 0, one_silent = 0;
    double sum_corr = 0, sum_ratio = 0;
    long nratio = 0;
    int bad_shown = 0;
    long run_start = -1;
    for (long k = from; k < last; k++) {
        long rf = mapf ? map[k] : k;
        if (rf < 0 || rf >= r.frames) continue;
        int n = r.cnt[rf] < o.cnt[k] ? r.cnt[rf] : o.cnt[k];
        if (n <= 2 * L + 16) continue;
        const float *a = r.s + r.off[rf], *b = o.s + o.off[k];
        double ma, mb, ra = rms(a, n, &ma), rb = rms(b, n, &mb);
        int sa = ra < 5e-3, sb = rb < 5e-3;
        int bad = 0;
        double best = -1;
        if (sa && sb) { both_silent++; }
        else if (sa || sb) { one_silent++; bad = 1; }
        else {
            sounding++;
            for (int lag = -L; lag <= L; lag++) {
                double num = 0, ea = 0, eb = 0;
                int i0 = lag < 0 ? -lag : 0, i1 = lag > 0 ? n - lag : n;
                for (int i = i0; i < i1; i++) {
                    double x = a[i] - ma, y = b[i + lag] - mb;
                    num += x * y; ea += x * x; eb += y * y;
                }
                double c = (ea > 0 && eb > 0) ? num / sqrt(ea * eb) : 0;
                if (c > best) best = c;
            }
            sum_corr += best;
            sum_ratio += rb / ra; nratio++;
            if (best >= 0.9) good++; else bad = 1;
        }
        if (dumpf) printf("%ld %.3f %.4f %.4f\n", k, best, ra, rb);
        if (bad && run_start < 0) run_start = k;
        if ((!bad || k == last - 1) && run_start >= 0) {
            if (bad_shown < 15)
                printf("  mismatch frames %ld-%ld (ref %ld-)%s\n", run_start, bad ? k : k - 1,
                       mapf ? map[run_start] : run_start, "");
            bad_shown++;
            run_start = -1;
        }
    }
    double frac = sounding ? (double)good / sounding : 1.0;
    printf("frames %ld-%ld: %ld sounding, %ld corr>=0.9 (%.2f%%), mean corr %.3f, level ratio our/ref %.3f; "
           "%ld both silent, %ld one side silent; %d mismatch runs\n",
           from, last - 1, sounding, good, 100 * frac, sounding ? sum_corr / sounding : 0,
           nratio ? sum_ratio / nratio : 0, both_silent, one_silent, bad_shown);
    return (frac >= 0.99 && one_silent == 0) ? 0 : 1;
}
