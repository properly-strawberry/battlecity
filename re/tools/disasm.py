"""Recursive-descent disassembler for the Battle City PRG.

Seeds: vectors, disasm_cfg entries/jump tables, and opcode addresses from
emulator coverage files (bit 0). Output:
  - a ca65-style listing (re/docs/battlecity.asm)
  - a byte-class map (re/docs/codemap.json)
The listing is re-assembled by asm_check.py and compared byte-for-byte with
the PRG.

usage: python3 disasm.py [coverage.bin ...]
"""
import json
import os
import sys

import disasm_cfg as cfg
from m6502 import OPS, LEN, BRANCHES, fmt_operand

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
import rom
PRG = rom.prg()
BASE = 0xC000

try:
    import symbols
    RAM_NAMES = symbols.RAM
    CODE_NAMES = symbols.CODE
    COMMENTS = getattr(symbols, 'COMMENTS', {})
except ImportError:
    RAM_NAMES, CODE_NAMES, COMMENTS = {}, {}, {}

IO_NAMES = {
    0x2000: 'PPUCTRL', 0x2001: 'PPUMASK', 0x2002: 'PPUSTATUS', 0x2003: 'OAMADDR',
    0x2004: 'OAMDATA', 0x2005: 'PPUSCROLL', 0x2006: 'PPUADDR', 0x2007: 'PPUDATA',
    0x4000: 'SQ1_VOL', 0x4001: 'SQ1_SWEEP', 0x4002: 'SQ1_LO', 0x4003: 'SQ1_HI',
    0x4004: 'SQ2_VOL', 0x4005: 'SQ2_SWEEP', 0x4006: 'SQ2_LO', 0x4007: 'SQ2_HI',
    0x4008: 'TRI_LINEAR', 0x400A: 'TRI_LO', 0x400B: 'TRI_HI',
    0x400C: 'NOISE_VOL', 0x400E: 'NOISE_LO', 0x400F: 'NOISE_HI',
    0x4010: 'DMC_FREQ', 0x4011: 'DMC_RAW', 0x4012: 'DMC_START', 0x4013: 'DMC_LEN',
    0x4014: 'OAMDMA', 0x4015: 'SND_CHN', 0x4016: 'JOY1', 0x4017: 'JOY2',
}


def rd(a):
    return PRG[a - BASE]


def rd16(a):
    return rd(a) | rd(a + 1) << 8


class Disasm:
    def __init__(self, coverage_files):
        self.cls = {}          # addr -> 'op' | 'arg' | 'jtab' | 'data'
        self.ins = {}          # addr -> length
        self.labels = {}       # addr -> kind ('sub','loc','tbl','dat')
        self.conflicts = []
        self.data_refs = {}    # addr -> set(instruction addrs)
        self.jtab_ranges = []  # (start, count, why)
        self.skips = set()     # BIT opcodes used as 1-byte skip prefixes
        self.cov = bytearray(0x4000)
        for f in coverage_files:
            c = open(f, 'rb').read()
            for i, b in enumerate(c):
                self.cov[i] |= b

    def label(self, a, kind):
        prio = {'sub': 3, 'loc': 2, 'tbl': 1, 'dat': 0}
        if a not in self.labels or prio[kind] > prio[self.labels[a]]:
            self.labels[a] = kind

    def mark_words(self, start, count, why, kind='tbl'):
        self.label(start, kind)
        self.jtab_ranges.append((start, count, why))
        targets = []
        for i in range(count):
            for k in (0, 1):
                self.cls[start + 2 * i + k] = 'jtab'
            targets.append(rd16(start + 2 * i))
        return targets

    def run(self):
        work = []
        for vec, name in cfg.VECTORS.items():
            t = rd16(vec)
            self.label(t, 'sub')
            work.append(t)
            self.mark_words(vec, 1, 'CPU vector ' + name, kind='dat')
        for a in cfg.ENTRIES:
            self.label(a, 'sub'); work.append(a)
        for t, (n, consumer, why) in cfg.JUMP_TABLES.items():
            for tg in self.mark_words(t, n, why):
                self.label(tg, 'sub'); work.append(tg)
        for site, (n, why) in cfg.INLINE_TABLES.items():
            for tg in self.mark_words(site + 3, n, why):
                self.label(tg, 'sub'); work.append(tg)
        for i in range(0x4000):
            if self.cov[i] & 1:
                work.append(BASE + i)
        while work:
            a = work.pop()
            self.trace(a, work)
        # data refs
        for a, n in self.ins.items():
            mn, mode = OPS[rd(a)]
            if mode in ('abs', 'abx', 'aby', 'ind') and n == 3:
                v = rd16(a + 1)
                if v >= BASE and mn not in ('JSR', 'JMP'):
                    self.data_refs.setdefault(v, set()).add(a)
        # immediate pointer pairs: LDA #hi / STA zp+1 / LDA #lo / STA zp (either
        # order, consecutive instructions) point a zero-page pointer at ROM data,
        # e.g. ptr ($11/$12) before draw_tiles.
        for a in self.ins:
            b = [rd(a + k) for k in range(8)] if a + 7 <= 0xFFFF else []
            if len(b) == 8 and b[0] == 0xA9 and b[2] == 0x85 and b[4] == 0xA9 and b[6] == 0x85 \
                    and all(a + k in self.ins for k in (2, 4, 6)) and abs(b[3] - b[7]) == 1:
                lo, hi = (b[1], b[5]) if b[3] < b[7] else (b[5], b[1])
                v = hi << 8 | lo
                if v >= BASE:
                    self.data_refs.setdefault(v, set()).add(a)
        for v in self.data_refs:
            if self.cls.get(v) in (None, 'data'):
                self.cls[v] = 'data'
                self.label(v, 'dat')
            elif self.cls.get(v) == 'jtab' and v not in self.labels:
                pass  # tbl+1 style reference, expressed as label+1
        # proven table extents (disasm_cfg.DATA_EXTENTS). Nothing else is
        # extended: an unreferenced byte stays unknown, however obvious.
        for t, (n, why) in cfg.DATA_EXTENTS.items():
            for a in range(t, t + n):
                if self.cls.get(a) in (None, 'data'):
                    self.cls[a] = 'data'
                else:
                    self.conflicts.append((a, 'data extent of $%04X overlaps %s' % (t, self.cls[a])))

    def trace(self, a, work):
        while BASE <= a <= 0xFFFF:
            if a in self.ins:
                return
            if self.cls.get(a) == 'arg' and self.cls.get(a - 1) == 'op' and rd(a - 1) in (0x2C, 0x24) \
                    and self.ins.get(a - 1) in (2, 3):
                # entry into the operand of a BIT used as a skip prefix:
                # demote the BIT to a 1-byte 'skip' and decode the hidden insn
                n0 = self.ins.pop(a - 1)
                self.cls[a - 1] = 'skip'
                for k in range(n0 - 1):
                    del self.cls[a + k]
                self.skips.add(a - 1)
            if self.cls.get(a) in ('arg', 'jtab'):
                self.conflicts.append((a, 'entry into ' + self.cls[a]))
                return
            if a in self.skips:
                a += 1
                continue
            op = rd(a)
            if op not in OPS:
                self.conflicts.append((a, 'illegal opcode $%02X' % op))
                return
            mn, mode = OPS[op]
            n = LEN[mode]
            if any(self.cls.get(a + k) in ('op', 'arg', 'jtab') for k in range(1, n)):
                if op in (0x2C, 0x24) and self.cls.get(a + 1) == 'op':
                    self.cls[a] = 'skip'
                    self.skips.add(a)
                    return  # falls into an already-decoded instruction
                self.conflicts.append((a, 'instruction overlaps existing bytes'))
                return
            self.ins[a] = n
            self.cls[a] = 'op'
            for k in range(1, n):
                self.cls[a + k] = 'arg'
            v = rd16(a + 1) if n == 3 else (rd(a + 1) if n == 2 else 0)
            if mn in BRANCHES:
                t = (a + 2 + ((v ^ 0x80) - 0x80)) & 0xFFFF
                self.label(t, 'loc'); work.append(t)
            if mn == 'JSR':
                self.label(v, 'sub'); work.append(v)
                if v in cfg.INLINE_DISPATCH:
                    if a not in cfg.INLINE_TABLES:
                        self.conflicts.append((a, 'inline dispatch call without table size'))
                    return
            if mn == 'JMP':
                if mode == 'abs':
                    self.label(v, 'loc'); work.append(v)
                return
            if mn in ('RTS', 'RTI', 'BRK'):
                return
            a += n

    # ---- output ----
    def name(self, a):
        if a in CODE_NAMES:
            return CODE_NAMES[a]
        k = self.labels.get(a)
        if k is None:
            return None
        return '%s_%04X' % (k, a)

    def ref(self, v, zp_ok):
        """Symbolic operand for address v (or None)."""
        if v in IO_NAMES:
            return IO_NAMES[v]
        if v < 0x800 and v in RAM_NAMES:
            return RAM_NAMES[v]
        if v >= BASE:
            nm = self.name(v)
            if nm:
                return nm
            # label+k for table interiors / mid-instruction refs
            for k in range(1, 4):
                nm = self.name(v - k)
                if nm:
                    return '%s+%d' % (nm, k)
        return None

    def listing(self):
        out = ['; Battle City (NES) PRG $C000-$FFFF -- generated by tools/disasm.py',
               '; Do not edit by hand: change disasm_cfg.py / symbols.py and regenerate.',
               '']
        for a in sorted(RAM_NAMES):
            if '+' in RAM_NAMES[a]:
                continue
            out.append('%-24s = $%04X' % (RAM_NAMES[a], a) if a > 0xFF else '%-24s = $%02X' % (RAM_NAMES[a], a))
        for a in sorted(IO_NAMES):
            out.append('%-24s = $%04X' % (IO_NAMES[a], a))
        out += ['', '.org $C000', '']
        a = BASE
        while a <= 0xFFFF:
            nm = self.name(a)
            if nm and self.cls.get(a) in ('op', 'jtab', 'data', 'skip', None):
                if a in COMMENTS:
                    out.append('')
                    for line in COMMENTS[a].splitlines():
                        out.append('; ' + line)
                out.append('%s:' % nm)
            c = self.cls.get(a)
            if c == 'op':
                n = self.ins[a]
                op = rd(a)
                mn, mode = OPS[op]
                v = rd16(a + 1) if n == 3 else (rd(a + 1) if n == 2 else 0)
                if mode == 'rel':
                    v = (a + 2 + ((v ^ 0x80) - 0x80)) & 0xFFFF
                sym = None
                if mode not in ('imp', 'acc', 'imm'):
                    sym = self.ref(v, mode in ('zp', 'zpx', 'zpy', 'izx', 'izy'))
                opnd = fmt_operand(mode, v, sym)
                if mode in ('abs', 'abx', 'aby') and v < 0x100:
                    opnd = 'a:' + opnd     # force absolute addressing
                text = ('    %s %s' % (mn.lower(), opnd)).rstrip()
                out.append('%-40s; %04X %s' % (text, a, ' '.join('%02X' % rd(a + k) for k in range(n))))
                a += n
            elif c == 'skip':
                out.append('%-40s; %04X %02X  skip next insn (BIT %s)' % (
                    '    .byte $%02X' % rd(a), a, rd(a), 'abs' if rd(a) == 0x2C else 'zp'))
                a += 1
            elif c == 'jtab':
                w = rd16(a)
                sym = self.ref(w, False) or '$%04X' % w
                out.append('%-40s; %04X' % ('    .word %s' % sym, a))
                a += 2
            else:
                # data / unknown run up to next label or class change
                kind = c
                start = a
                buf = []
                while a <= 0xFFFF and self.cls.get(a) == kind and len(buf) < 16:
                    if a != start and self.name(a):
                        break
                    buf.append(rd(a))
                    a += 1
                tag = 'data' if kind == 'data' else 'unknown'
                out.append('%-40s; %04X %s' % ('    .byte ' + ','.join('$%02X' % b for b in buf), start, tag))
        return '\n'.join(out) + '\n'

    def codemap(self):
        stats = {}
        for a in range(BASE, 0x10000):
            k = self.cls.get(a, 'unknown')
            stats[k] = stats.get(k, 0) + 1
        return {
            'stats': stats,
            'subs': sorted('%04X' % a for a, k in self.labels.items() if k == 'sub'),
            'conflicts': [('%04X' % a, why) for a, why in sorted(self.conflicts)],
            'jump_tables': [('%04X' % s, n, why) for s, n, why in sorted(self.jtab_ranges)],
            'classes': ''.join({'op': 'O', 'arg': 'a', 'jtab': 'J', 'data': 'D', 'skip': 'S'}.get(self.cls.get(a), '?')
                               for a in range(BASE, 0x10000)),
        }


def main():
    d = Disasm(sys.argv[1:])
    d.run()
    docs = os.path.join(ROOT, 'docs')
    open(os.path.join(docs, 'battlecity.asm'), 'w').write(d.listing())
    cm = d.codemap()
    json.dump(cm, open(os.path.join(docs, 'codemap.json'), 'w'), indent=1)
    print('stats', cm['stats'])
    print('subs', len(cm['subs']))
    for c in cm['conflicts']:
        print('conflict', c)


if __name__ == '__main__':
    main()
