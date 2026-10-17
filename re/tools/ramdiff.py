"""Compare two per-frame RAM dump files (N x 2048 bytes).

usage: ramdiff.py A.bin B.bin [--shift K] [--ignore-stack]
--shift K compares A[f] with B[f+K] (frame alignment offset).
Prints the first differing frames and a summary."""
import sys


def main():
    a = open(sys.argv[1], 'rb').read()
    b = open(sys.argv[2], 'rb').read()
    shift = 0
    if '--shift' in sys.argv:
        shift = int(sys.argv[sys.argv.index('--shift') + 1])
    ign = '--ignore-stack' in sys.argv
    na, nb = len(a) // 2048, len(b) // 2048
    first = None
    bad = 0
    for f in range(na):
        g = f + shift
        if g < 0 or g >= nb:
            continue
        fa = a[f * 2048:(f + 1) * 2048]
        fb = b[g * 2048:(g + 1) * 2048]
        diffs = [i for i in range(2048) if fa[i] != fb[i] and not (ign and 0x100 <= i < 0x180)]
        if diffs:
            bad += 1
            if first is None:
                first = f
                print('first diff at frame %d (B frame %d): %d bytes' % (f, g, len(diffs)))
                for i in diffs[:20]:
                    print('  $%04X A=%02X B=%02X' % (i, fa[i], fb[i]))
    print('%d of %d frames differ' % (bad, na))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
