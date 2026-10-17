"""Linear disassembly of a PRG address range (quick inspection helper)."""
import sys
from m6502 import OPS, LEN, fmt_operand
import rom
prg = rom.prg()
def rd(a): return prg[a & 0x3fff]
def dis(a, end):
    out = []
    while a < end:
        op = rd(a)
        if op not in OPS:
            out.append('%04X  %02X        .byte $%02X' % (a, op, op)); a += 1; continue
        mn, mode = OPS[op]; n = LEN[mode]
        b = [rd(a + i) for i in range(n)]
        v = (b[1] | b[2] << 8) if n == 3 else (b[1] if n == 2 else 0)
        if mode == 'rel': v = (a + 2 + ((v ^ 0x80) - 0x80)) & 0xffff
        out.append('%04X  %-9s %s %s' % (a, ' '.join('%02X' % x for x in b), mn, fmt_operand(mode, v)))
        a += n
    return out
if __name__ == '__main__':
    a = int(sys.argv[1], 16); e = int(sys.argv[2], 16)
    print('\n'.join(dis(a, e)))
