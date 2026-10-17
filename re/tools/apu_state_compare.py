"""Compare our APU's channel state with FCEUX's, frame by frame.

usage: python3 tools/apu_state_compare.py OURS.txt FCEUX.txt

OURS:  `build/apucheck ROM MOVIE FRAMES PREFIX --fceux-timing --state 0 N`
FCEUX: `third_party/fceux_headless/fceux_headless ROM MOVIE FRAMES RAM --apustate 0 N`
       (values from FCEUX's own save state at the end of each frame)

Fields: pulse periods, length counters and envelope levels; triangle length
and linear counter; noise length and envelope level. With --fceux-timing
both use the same frame-counter step lengths; what is left is a constant
difference of up to ~100 CPU cycles in where the steps fall, so a clock
can land on the other side of a register write or of the frame end. That
changes a counter by exactly one step, until the next write that reloads
it (observed on fceux_playthrough1: every count difference is +-1;
lengths mostly for 1 frame, envelopes for 2-6). Pass: every count field
differs by at most 1 and every field matches in >= 95% of frames."""
import sys

FIELDS = ['p1per', 'p1len', 'p1env', 'p2per', 'p2len', 'p2env', 'tlen', 'tlin', 'nlen', 'nenv']
FX = {'p1per': 'CRF1', 'p1len': 'LEN0', 'p1env': 'E0DV', 'p2per': 'CRF2', 'p2len': 'LEN1',
      'p2env': 'E1DV', 'tlen': 'LEN2', 'tlin': 'TRIC', 'nlen': 'LEN3', 'nenv': 'E2DV'}


def main():
    ours = {}
    for ln in open(sys.argv[1]):
        f = ln.split()
        if len(f) == 11 and f[0].isdigit():
            ours[int(f[0])] = dict(zip(FIELDS, map(int, f[1:])))
    fx = {}
    for ln in open(sys.argv[2]):
        f = ln.split()
        if not f or not f[0].isdigit() or '=' not in ln:
            continue
        d = dict(x.split('=') for x in f[1:])
        fx[int(f[0])] = {k: int(d[t], 16) for k, t in FX.items()}
        fx[int(f[0])]['p1per'] &= 0x7FF
        fx[int(f[0])]['p2per'] &= 0x7FF
    frames = sorted(set(ours) & set(fx))
    worst = 1.0
    shown = 0
    ok = True
    for k in FIELDS:
        bad = [fr for fr in frames if ours[fr][k] != fx[fr][k]]
        rate = 1 - len(bad) / len(frames)
        worst = min(worst, rate)
        big = [fr for fr in bad if 'per' not in k and abs(ours[fr][k] - fx[fr][k]) > 1]
        if big:
            ok = False
        print('%-6s %6.2f%% match (%d of %d frames differ%s)' % (
            k, 100 * rate, len(bad), len(frames), ', %d by more than 1' % len(big) if big else ''))
        for fr in bad[:3]:
            if shown < 24:
                print('        frame %d: ours %d, FCEUX %d' % (fr, ours[fr][k], fx[fr][k]))
                shown += 1
    ok = ok and worst >= 0.95
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
