"""Mechanical 6502 -> C translator (baseline layer of the reconstruction).

Produces src/mech/mech.c: one C function per subroutine entry
(vectors, JSR targets, jump-table targets). Each 6502 instruction becomes
one C statement that uses the runtime in src/mech/rt.h, so the code is
instruction-exact. Function bodies hold all code reachable from the entry
through fall-through, branches and JMP abs, so shared tails are duplicated.
JMP (ind) becomes a dispatch by runtime address.

Functions listed in src/lifted/*.lifted are skipped. Their hand-written
C replacements provide sub_XXXX with the same register contract. Since
2026-10-15 every routine is lifted, so the output holds only prototypes
and mech_dispatch (address -> sub_XXXX, used by register-level adapters
such as $ECD0 and by the test harnesses); no translated code is left.

usage: python3 lift.py [--out PATH] [--lifted-dir DIR ...]
  default: --out src/mech/mech.c --lifted-dir src/lifted
"""
import os
import re
import sys

import disasm_cfg as cfg
from disasm import Disasm, rd, rd16
from m6502 import OPS, LEN, BRANCHES

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Back-edges of the original's wait-for-vblank loops: (branch addr) -> note
WAIT_BRANCHES = {
    0xC07C: 'reset: poll PPUSTATUS for vblank (PPU warm-up)',
    0xD5F8: 'poll PPUSTATUS for vblank (NMI disabled)',
    0xD8FA: 'wait until NMI advances frame counter $0B',
}

BR_COND = {'BPL': '!fN', 'BMI': 'fN', 'BVC': '!fV', 'BVS': 'fV',
           'BCC': '!fC', 'BCS': 'fC', 'BNE': '!fZ', 'BEQ': 'fZ'}


def lifted_set(dirs):
    """Union of all *.lifted lists (one hex address per line) in dirs."""
    import glob
    s = set()
    for d in dirs:
        for p in sorted(glob.glob(os.path.join(d, '*.lifted'))):
            for ln in open(p):
                ln = ln.split('#')[0].strip()
                if ln:
                    s.add(int(ln, 16))
    return s


def ea_expr(mode, a):
    """C expression for the effective address."""
    if mode == 'zp':
        return '0x%02X' % rd(a + 1)
    if mode == 'zpx':
        return '(uint8_t)(0x%02X + X)' % rd(a + 1)
    if mode == 'zpy':
        return '(uint8_t)(0x%02X + Y)' % rd(a + 1)
    if mode == 'abs':
        return '0x%04X' % rd16(a + 1)
    if mode == 'abx':
        return '(uint16_t)(0x%04X + X)' % rd16(a + 1)
    if mode == 'aby':
        return '(uint16_t)(0x%04X + Y)' % rd16(a + 1)
    if mode == 'izx':
        return 'ZP16(0x%02X + X)' % rd(a + 1)
    if mode == 'izy':
        return '(uint16_t)(ZP16(0x%02X) + Y)' % rd(a + 1)
    raise ValueError(mode)


def operand(mode, a):
    if mode == 'imm':
        return '0x%02X' % rd(a + 1)
    return 'RD(%s)' % ea_expr(mode, a)


LIFTED = set()


def stmt(a, entries):
    """C statement(s) for the instruction at a. Returns (code, ends_flow)."""
    op = rd(a)
    mn, mode = OPS[op]
    n = LEN[mode]
    nxt = a + n
    if mn in BRANCHES:
        t = (a + 2 + ((rd(a + 1) ^ 0x80) - 0x80)) & 0xFFFF
        cond = BR_COND[mn]
        if a in WAIT_BRANCHES:
            return 'if (%s) { hal_wait_vblank(); goto L_%04X; }' % (cond, t), False
        return 'if (%s) goto L_%04X;' % (cond, t), False
    if mn == 'JMP':
        if mode == 'abs':
            return 'goto L_%04X;' % rd16(a + 1), True
        # indirect, with the 6502 page-wrap bug
        p = rd16(a + 1)
        hi = (p & 0xFF00) | ((p + 1) & 0xFF)
        return 'mech_dispatch((uint16_t)(RD(0x%04X) | RD(0x%04X) << 8)); return;' % (p, hi), True
    if mn == 'JSR':
        if rd16(a + 1) in LIFTED:
            # lifted C does not use the 6502 stack: plain call
            return 'sub_%04X();' % rd16(a + 1), False
        return 'JSR(0x%04X, sub_%04X);' % (a + 2, rd16(a + 1)), False
    if mn == 'RTS':
        return 'RTS();', True
    if mn == 'RTI':
        return 'RTI();', True
    if mn == 'BRK':
        return 'mech_brk(0x%04X); return;' % a, True
    simple = {
        'CLC': 'fC = 0;', 'SEC': 'fC = 1;', 'CLI': 'fI = 0;', 'SEI': 'fI = 1;',
        'CLV': 'fV = 0;', 'CLD': 'fD = 0;', 'SED': 'fD = 1;', 'NOP': ';',
        'TAX': 'X = A; SZN(X);', 'TAY': 'Y = A; SZN(Y);', 'TXA': 'A = X; SZN(A);',
        'TYA': 'A = Y; SZN(A);', 'TSX': 'X = S; SZN(X);', 'TXS': 'S = X;',
        'INX': 'X++; SZN(X);', 'INY': 'Y++; SZN(Y);', 'DEX': 'X--; SZN(X);', 'DEY': 'Y--; SZN(Y);',
        'PHA': 'PUSH(A);', 'PLA': 'A = PULL(); SZN(A);', 'PHP': 'PUSH(GETP());',
        'PLP': 'SETP(PULL());',
    }
    if mn in simple:
        return simple[mn], False
    if mode == 'acc':
        return 'A = op_%s(A);' % mn.lower(), False
    if mn in ('LDA', 'LDX', 'LDY'):
        r = mn[2]
        return '%s = %s; SZN(%s);' % (r, operand(mode, a), r), False
    if mn in ('STA', 'STX', 'STY'):
        return 'WR(%s, %s);' % (ea_expr(mode, a), mn[2]), False
    if mn in ('AND', 'ORA', 'EOR'):
        o = {'AND': '&', 'ORA': '|', 'EOR': '^'}[mn]
        return 'A %s= %s; SZN(A);' % (o, operand(mode, a)), False
    if mn in ('ADC', 'SBC'):
        return 'op_%s(%s);' % (mn.lower(), operand(mode, a)), False
    if mn in ('CMP', 'CPX', 'CPY'):
        r = {'CMP': 'A', 'CPX': 'X', 'CPY': 'Y'}[mn]
        return 'op_cmp(%s, %s);' % (r, operand(mode, a)), False
    if mn == 'BIT':
        return 'op_bit(%s);' % operand(mode, a), False
    if mn in ('ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC'):
        return 'RMW(%s, op_%s);' % (ea_expr(mode, a), mn.lower()), False
    raise ValueError('unhandled %s %s at %04X' % (mn, mode, a))


def body(entry, d):
    """All instruction addresses reachable from entry inside one function."""
    seen = set()
    work = [entry]
    while work:
        a = work.pop()
        while a not in seen:
            seen.add(a)
            op = rd(a)
            mn, mode = OPS[op]
            n = LEN[mode]
            if mn in BRANCHES:
                work.append((a + 2 + ((rd(a + 1) ^ 0x80) - 0x80)) & 0xFFFF)
            if mn == 'JMP':
                if mode == 'abs':
                    work.append(rd16(a + 1))
                break
            if mn in ('RTS', 'RTI', 'BRK'):
                break
            if mn == 'JSR' and rd16(a + 1) in cfg.INLINE_DISPATCH:
                break
            a += n
    return sorted(seen)


def main():
    args = sys.argv[1:]
    out_path = os.path.join(ROOT, 'src', 'mech', 'mech.c')
    dirs = [os.path.join(ROOT, 'src', 'lifted')]
    i = 0
    while i < len(args):
        if args[i] == '--out':
            out_path = args[i + 1]; i += 2
        elif args[i] == '--lifted-dir':
            dirs.append(args[i + 1]); i += 2
        else:
            raise SystemExit('unknown argument ' + args[i])
    d = Disasm([])
    d.run()
    lifted = lifted_set(dirs)
    LIFTED.update(lifted)
    entries = sorted(a for a, k in d.labels.items() if k == 'sub')
    out = ['/* GENERATED by tools/lift.py from the verified disassembly -- do not edit.',
           ' * Instruction-exact C translation of the original 6502 code. Each',
           ' * function is named after its original entry address. */',
           '#include "%s"' % os.path.relpath(os.path.join(ROOT, 'src', 'mech', 'rt.h'), os.path.dirname(os.path.abspath(out_path))), '']
    for e in entries:
        out.append('void sub_%04X(void);' % e)
    out.append('void mech_brk(uint16_t at);')
    out.append('')
    count = 0
    for e in entries:
        if e in lifted:
            continue
        count += 1
        b = body(e, d)
        out.append('/* $%04X */' % e)
        out.append('void sub_%04X(void) {' % e)
        if b[0] != e:
            out.append('    goto L_%04X;' % e)
        targets = set()
        for a in b:
            mn, mode = OPS[rd(a)]
            if mn in BRANCHES:
                targets.add((a + 2 + ((rd(a + 1) ^ 0x80) - 0x80)) & 0xFFFF)
            if mn == 'JMP' and mode == 'abs':
                targets.add(rd16(a + 1))
        # emit in address order; insert goto at gaps so fall-through is exact
        prev_end = None
        prev_falls = False
        first = True
        for a in b:
            if not first and prev_falls and prev_end != a:
                out.append('    goto L_%04X;' % prev_end)
                targets.add(prev_end)
            first = False
            code, ends = stmt(a, entries)
            mn, mode = OPS[rd(a)]
            lab = 'L_%04X: ' % a
            code = 'MTRACE(0x%04X); ' % a + code
            out.append('%-9s%-64s/* %04X %s */' % (lab, code, a, ' '.join('%02X' % rd(a + k) for k in range(LEN[mode]))))
            prev_end = a + LEN[mode]
            prev_falls = not ends and not (mn == 'JSR' and rd16(a + 1) in cfg.INLINE_DISPATCH)
            if mn == 'JSR' and rd16(a + 1) in cfg.INLINE_DISPATCH:
                out[-1] = out[-1].replace(code, code + ' /* inline table: never returns here */')
        if prev_falls:
            out.append('    goto L_%04X;' % prev_end)
        out.append('    (void)0;')
        out.append('}')
        out.append('')
    # unused labels are fine in C (warning only); suppress via pragma
    out.insert(4, '#pragma GCC diagnostic ignored "-Wunused-label"')
    # dispatcher
    out.append('void mech_dispatch(uint16_t addr) {')
    out.append('    switch (addr) {')
    for e in entries:
        if e in lifted:
            # a JMP (ind) into lifted code: its return pops the frame the
            # original RTS would have popped
            out.append('    case 0x%04X: sub_%04X(); S += 2; return;' % (e, e))
        else:
            out.append('    case 0x%04X: sub_%04X(); return;' % (e, e))
    out.append('    default: mech_bad_dispatch(addr); return;')
    out.append('    }')
    out.append('}')
    out.insert(out.index('void mech_brk(uint16_t at);') + 1, 'void mech_bad_dispatch(uint16_t addr);')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    open(out_path, 'w').write('\n'.join(out) + '\n')
    print('wrote %s: %d functions (%d lifted, skipped)' % (out_path, count, len(lifted)))


if __name__ == '__main__':
    main()
