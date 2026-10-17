"""Register contracts for every subroutine (inter-procedural liveness).

For each subroutine f:
  in          registers/flags f reads before writing (on some path)
  out         registers/flags f may write that some caller reads after f returns
  preserves   registers live in a caller across the call that f never writes
  clobbers    registers f may write (may-define, incl. callees)
  always_sets registers written on every path to a return (must-define)

Registers tracked: A X Y C Z N V. A call site is modelled as
live_before = (live_after - mustdef(f)) | in(f), so registers that merely
pass through f do not leak into its inputs.

Static analysis over all paths is conservative ("may"). Results go to
docs/contracts.json.

usage: python3 contracts.py [-v]
"""
import json
import os
import sys

import disasm_cfg as cfg
from disasm import Disasm, rd, rd16
from lift import body
from m6502 import OPS, LEN, BRANCHES

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALL = frozenset('AXYCZNV')
NZ = {'N', 'Z'}
ORDER = 'AXYCZNV'


def du(a):
    """(uses, defs) for the instruction at a (calls handled by the caller)."""
    mn, mode = OPS[rd(a)]
    idx = {'zpx': {'X'}, 'abx': {'X'}, 'izx': {'X'}, 'zpy': {'Y'}, 'aby': {'Y'}, 'izy': {'Y'}}.get(mode, set())
    u, d = set(idx), set()
    if mn in ('LDA', 'LDX', 'LDY'):
        d = {mn[2]} | NZ
    elif mn in ('STA', 'STX', 'STY'):
        u |= {mn[2]}
    elif mn in ('AND', 'ORA', 'EOR'):
        u |= {'A'}; d = {'A'} | NZ
    elif mn in ('ADC', 'SBC'):
        u |= {'A', 'C'}; d = {'A', 'C', 'V'} | NZ
    elif mn in ('CMP', 'CPX', 'CPY'):
        u |= {{'CMP': 'A', 'CPX': 'X', 'CPY': 'Y'}[mn]}; d = {'C'} | NZ
    elif mn == 'BIT':
        u |= {'A'}; d = {'V'} | NZ
    elif mn in ('ASL', 'LSR'):
        u |= {'A'} if mode == 'acc' else set()
        d = {'C'} | NZ | ({'A'} if mode == 'acc' else set())
    elif mn in ('ROL', 'ROR'):
        u |= {'C'} | ({'A'} if mode == 'acc' else set())
        d = {'C'} | NZ | ({'A'} if mode == 'acc' else set())
    elif mn in ('INC', 'DEC'):
        d = set(NZ)
    elif mn in ('INX', 'DEX'):
        u |= {'X'}; d = {'X'} | NZ
    elif mn in ('INY', 'DEY'):
        u |= {'Y'}; d = {'Y'} | NZ
    elif mn in ('TAX', 'TAY'):
        u |= {'A'}; d = {mn[2]} | NZ
    elif mn == 'TXA':
        u |= {'X'}; d = {'A'} | NZ
    elif mn == 'TYA':
        u |= {'Y'}; d = {'A'} | NZ
    elif mn == 'TSX':
        d = {'X'} | NZ
    elif mn == 'TXS':
        u |= {'X'}
    elif mn == 'PHA':
        u |= {'A'}
    elif mn == 'PLA':
        d = {'A'} | NZ
    elif mn == 'PHP':
        u |= {'C', 'Z', 'N', 'V'}
    elif mn == 'PLP':
        d = {'C', 'Z', 'N', 'V'}
    elif mn in ('CLC', 'SEC'):
        d = {'C'}
    elif mn == 'CLV':
        d = {'V'}
    elif mn in BRANCHES:
        u |= {{'BPL': 'N', 'BMI': 'N', 'BVC': 'V', 'BVS': 'V', 'BCC': 'C', 'BCS': 'C', 'BNE': 'Z', 'BEQ': 'Z'}[mn]}
    return u, d


def succs(a):
    mn, mode = OPS[rd(a)]
    n = LEN[mode]
    if mn in BRANCHES:
        return [a + n, (a + 2 + ((rd(a + 1) ^ 0x80) - 0x80)) & 0xFFFF]
    if mn == 'JMP':
        return [rd16(a + 1)] if mode == 'abs' else []
    if mn in ('RTS', 'RTI', 'BRK'):
        return []
    if mn == 'JSR' and rd16(a + 1) in cfg.INLINE_DISPATCH:
        return []
    return [a + n]


def site_targets(site):
    n, why = cfg.INLINE_TABLES[site]
    return {rd16(site + 3 + 2 * i) for i in range(n)}


def analyse():
    d = Disasm([])
    d.run()
    entries = sorted(a for a, k in d.labels.items() if k == 'sub')
    bodies = {e: body(e, d) for e in entries}
    tail = {}
    for e, b in bodies.items():
        tt = set()
        for a in b:
            mn, mode = OPS[rd(a)]
            if mn == 'JMP' and mode == 'ind':
                for t, (n, consumer, why) in cfg.JUMP_TABLES.items():
                    if consumer == a:
                        tt |= {rd16(t + 2 * i) for i in range(n)}
                if a == 0xECE3:   # the $ECD0 inline dispatcher's JMP ($FD)
                    for site in cfg.INLINE_TABLES:
                        tt |= site_targets(site)
        tail[e] = tt

    # may-define
    maydef = {e: set() for e in entries}
    for _ in range(100):
        ch = False
        for e in entries:
            m = set()
            for a in bodies[e]:
                mn, mode = OPS[rd(a)]
                if mn == 'JSR':
                    m |= maydef.get(rd16(a + 1), set())
                    if rd16(a + 1) in cfg.INLINE_DISPATCH:
                        for t in site_targets(a):
                            m |= maydef.get(t, set())
                else:
                    m |= du(a)[1]
            for t in tail[e]:
                m |= maydef.get(t, set())
            if m != maydef[e]:
                maydef[e] = m; ch = True
        if not ch:
            break

    # must-define (forward, intersection over paths to every return)
    mustdef = {e: set(ALL) for e in entries}
    for _ in range(100):
        ch = False
        for e in entries:
            b = set(bodies[e])
            dfin = {e: set()}
            work = [e]
            rets = []
            while work:
                a = work.pop()
                cur = set(dfin[a])
                mn, mode = OPS[rd(a)]
                if mn == 'JSR':
                    callee = rd16(a + 1)
                    if callee in cfg.INLINE_DISPATCH:
                        m = set(ALL)
                        for t in site_targets(a):
                            m &= mustdef.get(t, set())
                        rets.append(cur | m)
                        continue
                    cur |= mustdef.get(callee, set())
                else:
                    cur |= du(a)[1]
                if mn in ('RTS', 'RTI'):
                    rets.append(cur)
                if mn == 'JMP' and mode == 'ind':
                    m = set(ALL)
                    for t in tail[e]:
                        m &= mustdef.get(t, set())
                    rets.append(cur | m)
                for sc in succs(a):
                    if sc not in b:
                        continue
                    if sc not in dfin:
                        dfin[sc] = set(cur); work.append(sc)
                    else:
                        nw = dfin[sc] & cur
                        if nw != dfin[sc]:
                            dfin[sc] = nw; work.append(sc)
            m = set(ALL)
            for r in rets:
                m &= r
            if m != mustdef[e]:
                mustdef[e] = m; ch = True
        if not ch:
            break

    inputs = {e: set() for e in entries}
    outputs = {e: set() for e in entries}

    def live_in(e):
        b = bodies[e]
        live = {a: set() for a in b}
        changed = True
        while changed:
            changed = False
            for a in reversed(b):
                mn, mode = OPS[rd(a)]
                out = set()
                for sc in succs(a):
                    out |= live.get(sc, set())
                if mn == 'RTS':
                    out = set(outputs[e] & maydef[e])
                elif mn == 'RTI':
                    out = set()
                elif mn == 'JMP' and mode == 'ind':
                    for t in tail[e]:
                        out |= inputs[t]
                if mn == 'JSR':
                    callee = rd16(a + 1)
                    if callee in cfg.INLINE_DISPATCH:
                        new = {'A'}
                        for t in site_targets(a):
                            new |= inputs[t]
                    else:
                        new = (out - mustdef[callee]) | inputs[callee]
                else:
                    u, dd = du(a)
                    new = (out - dd) | u
                if new != live[a]:
                    live[a] = new
                    changed = True
        return live

    for _ in range(100):
        changed = False
        newout = {e: set() for e in entries}
        newin = {}
        for e in entries:
            live = live_in(e)
            newin[e] = live[e]
            for a in bodies[e]:
                mn, mode = OPS[rd(a)]
                if mn == 'JSR':
                    callee = rd16(a + 1)
                    if callee in cfg.INLINE_DISPATCH:
                        for t in site_targets(a):
                            newout[t] |= outputs[e]
                    elif a + 3 in live:
                        newout[callee] |= live[a + 3]
                if mn == 'JMP' and mode == 'ind':
                    for t in tail[e]:
                        newout[t] |= outputs[e]
        for e in entries:
            if newout[e] != outputs[e] or newin[e] != inputs[e]:
                changed = True
            outputs[e] = newout[e]
            inputs[e] = newin[e]
        if not changed:
            break

    res = {}
    for e in entries:
        res['%04X' % e] = {
            'in': ''.join(r for r in ORDER if r in inputs[e]),
            'out': ''.join(r for r in ORDER if r in outputs[e] & maydef[e]),
            'preserves': ''.join(r for r in ORDER if r in outputs[e] - maydef[e]),
            'clobbers': ''.join(r for r in ORDER if r in maydef[e]),
            'always_sets': ''.join(r for r in ORDER if r in mustdef[e]),
        }
    return res


def main():
    res = analyse()
    json.dump(res, open(os.path.join(ROOT, 'docs', 'contracts.json'), 'w'), indent=1, sort_keys=True)
    import symbols
    for k, r in sorted(res.items()):
        e = int(k, 16)
        nm = symbols.CODE.get(e, 'sub_%04X' % e)
        if r['in'] or r['out'] or '-v' in sys.argv:
            print('%04X %-28s in=%-5s out=%-5s keeps=%-4s clobbers=%s' % (e, nm, r['in'], r['out'], r['preserves'], r['clobbers']))


if __name__ == '__main__':
    main()
