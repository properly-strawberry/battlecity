"""Generate input movies (nesrun format: 'COUNT BUTTONS' lines).

usage: mkmovie.py SEED MODE FRAMES > movie.txt
MODE: 1p | 2p | cons (construction) | idle
Random play: hold a direction for 4-60 frames, fire (A/B) frequently,
occasionally pause (Start)."""
import random
import sys


def main():
    seed, mode, frames = int(sys.argv[1]), sys.argv[2], int(sys.argv[3])
    r = random.Random(seed)
    out = []
    t = 0

    def emit(n, b, b2=None):
        nonlocal t
        if b2 is None and mode == '2p':
            b2 = r.choice(['U', 'D', 'L', 'R', '']) + ('A' if r.random() < 0.5 else '')
        out.append('%d %s %s' % (n, b or '.', b2 or '.'))
        t += n

    emit(200, '')
    if mode == 'idle':
        emit(frames, '')
    else:
        # skip the title scroll, then choose a menu entry with Select
        emit(2, 'S'); emit(30, '')
        for _ in range({'1p': 0, '2p': 1, 'cons': 2}[mode]):
            emit(2, 's'); emit(10, '')
        emit(2, 'S'); emit(10, '')
    while t < frames:
        d = r.choice(['U', 'D', 'L', 'R', 'U', 'D', 'L', 'R', ''])
        n = r.randint(4, 60)
        fire = r.random() < 0.7
        # alternate fire on/off so each press registers
        while n > 0 and t < frames:
            k = min(n, r.randint(2, 8))
            emit(k, d + (r.choice('AB') if fire and r.random() < 0.5 else ''))
            n -= k
        if r.random() < 0.01:
            emit(2, 'S'); emit(r.randint(10, 120), ''); emit(2, 'S')
        if mode == 'cons' and r.random() < 0.02:
            emit(2, 'S')  # leave construction mode
    print('\n'.join(out))


if __name__ == '__main__':
    main()
