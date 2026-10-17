"""Compare FCEUX and reference-emulator RAM dumps (per frame).

Transient differences are expected in frames where the main thread is
mid-way through work at the frame boundary (cycle-level timing differences
between the two emulators). A real divergence persists: the check fails
if any byte differs for more than 4 consecutive frames.
Stack bytes $0160-$017F are ignored (they hold the interrupted PC)."""
import sys

a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
name = sys.argv[3]
n = min(len(a), len(b)) // 2048
bad = 0
run = {}
worst = (0, None, None)
for f in range(n):
    fa, fb = a[f * 2048:(f + 1) * 2048], b[f * 2048:(f + 1) * 2048]
    cur = set()
    if fa != fb:
        cur = {i for i in range(2048) if fa[i] != fb[i] and not 0x160 <= i < 0x180}
    if cur:
        bad += 1
    run = {i: run.get(i, 0) + 1 for i in cur}
    for i, r in run.items():
        if r > worst[0]:
            worst = (r, i, f)
div = worst[0] > 4
print('%-24s oracle:   %d/%d frames differ transiently (longest run %d frames%s)%s' % (
    name, bad, n, worst[0], ' at $%04X frame %d' % (worst[1], worst[2]) if worst[1] is not None else '',
    '; DIVERGED' if div else ''))
sys.exit(1 if div else 0)
