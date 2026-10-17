"""Convert an FCEUX .fm2 movie to the project's movie format.

usage: fm2_to_movie.py IN.fm2 OUT.txt

fm2 input lines look like  |c|RLDUTSBA|RLDUTSBA||  (one per frame): any
character other than '.' or ' ' is a pressed button. Our format is run-length
"COUNT P1 P2" lines with letters from "ABsSUDLR" (s = Select, S = Start).
Only power-on movies are supported: a movie that starts from a savestate,
or one with soft-reset/power commands mid-run, is rejected.
"""
import sys

FM2_ORDER = 'RLDUTSBA'          # fm2 column order, left to right
OUR = {'R': 'R', 'L': 'L', 'D': 'D', 'U': 'U', 'T': 'S', 'S': 's', 'B': 'B', 'A': 'A'}
OUT_ORDER = 'ABsSUDLR'


def pad(field):
    pressed = {OUR[FM2_ORDER[i]] for i, ch in enumerate(field[:8]) if ch not in '. '}
    s = ''.join(b for b in OUT_ORDER if b in pressed)
    return s or '.'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    frames = []
    header = {}
    for ln in open(src, encoding='utf-8', errors='replace'):
        ln = ln.rstrip('\n')
        if ln.startswith('|'):
            parts = ln.split('|')
            cmd = int(parts[1] or '0')
            if cmd & ~0x02 and frames:
                raise SystemExit('frame %d: unsupported command %d (reset/disk/etc.)' % (len(frames), cmd))
            p1 = pad(parts[2]) if len(parts) > 2 and parts[2] else '.'
            p2 = pad(parts[3]) if len(parts) > 3 and parts[3] else '.'
            frames.append((p1, p2))
        elif ' ' in ln:
            k, v = ln.split(' ', 1)
            header[k] = v
    if header.get('savestate'):
        raise SystemExit('movie starts from a savestate; record from power-on')
    out = ['# converted from %s' % src,
           '# romFilename=%s romChecksum=%s frames=%d' % (header.get('romFilename', '?'), header.get('romChecksum', '?'), len(frames))]
    i = 0
    while i < len(frames):
        j = i
        while j < len(frames) and frames[j] == frames[i]:
            j += 1
        out.append('%d %s %s' % (j - i, frames[i][0], frames[i][1]))
        i = j
    open(dst, 'w').write('\n'.join(out) + '\n')
    print('%s: %d frames' % (dst, len(frames)))


if __name__ == '__main__':
    main()
