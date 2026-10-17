"""Two-pass assembler for the generated listing; verifies that
docs/battlecity.asm reassembles to exactly the ROM's PRG (tools/rom.py).

Supports the subset disasm.py emits: `NAME = $XX`, `.org`, labels,
`.byte`, `.word`, instructions with `a:` forced-absolute prefix and
`label+N` expressions."""
import os
import re
import sys

from m6502 import ENC, OPS

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def parse_num(tok, syms):
    tok = tok.strip()
    m = re.fullmatch(r'([A-Za-z_][\w]*)\s*([+-]\s*\d+)?', tok)
    if m:
        base = syms.get(m.group(1))
        if base is None:
            return None
        return base + (int(m.group(2).replace(' ', '')) if m.group(2) else 0)
    if tok.startswith('$'):
        return int(tok[1:], 16)
    return int(tok)


def operand_mode(mn, text):
    """Return (mode candidates, expr, force_abs)."""
    t = text.strip()
    if t == '':
        return (['imp'], None, False)
    if t == 'a':
        return (['acc'], None, False)
    if t.startswith('#'):
        return (['imm'], t[1:], False)
    m = re.fullmatch(r'\((.+),x\)', t)
    if m:
        return (['izx'], m.group(1), False)
    m = re.fullmatch(r'\((.+)\),y', t)
    if m:
        return (['izy'], m.group(1), False)
    m = re.fullmatch(r'\((.+)\)', t)
    if m:
        return (['ind'], m.group(1), False)
    force = t.startswith('a:')
    if force:
        t = t[2:]
    if t.endswith(',x'):
        return (['abx'] if force else ['zpx', 'abx'], t[:-2], force)
    if t.endswith(',y'):
        return (['aby'] if force else ['zpy', 'aby'], t[:-2], force)
    if mn in ('BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ'):
        return (['rel'], t, False)
    return (['abs'] if force else ['zp', 'abs'], t, force)


def assemble(lines):
    syms = {}
    # pass 0: equates
    for ln in lines:
        code = ln.split(';', 1)[0].strip()
        m = re.fullmatch(r'([A-Za-z_]\w*)\s*=\s*(\$[0-9A-Fa-f]+)', code)
        if m:
            syms[m.group(1)] = int(m.group(2)[1:], 16)
    out = {}
    for pas in (1, 2):
        pc = 0
        for ln in lines:
            code = ln.split(';', 1)[0].strip()
            if not code or re.fullmatch(r'[A-Za-z_]\w*\s*=.*', code):
                continue
            if code.startswith('.org'):
                pc = int(code.split()[1][1:], 16)
                continue
            m = re.fullmatch(r'([A-Za-z_][\w]*):', code)
            if m:
                if pas == 1:
                    if m.group(1) in syms:
                        raise SystemExit('duplicate label ' + m.group(1))
                    syms[m.group(1)] = pc
                continue
            if code.startswith('.byte'):
                vals = [parse_num(x, syms) for x in code[5:].split(',')]
                for v in vals:
                    out[pc] = v; pc += 1
                continue
            if code.startswith('.word'):
                for x in code[5:].split(','):
                    v = parse_num(x, syms) or 0
                    out[pc] = v & 0xFF; out[pc + 1] = v >> 8; pc += 2
                continue
            parts = code.split(None, 1)
            mn = parts[0].upper()
            cands, expr, force = operand_mode(mn, parts[1] if len(parts) > 1 else '')
            v = parse_num(expr, syms) if expr is not None else 0
            if v is None:
                if pas == 2:
                    raise SystemExit('undefined symbol in: ' + ln)
                v = 0xFFFF
            mode = None
            for cand in cands:
                if cand in ('zp', 'zpx', 'zpy') and (v > 0xFF or (mn, cand) not in ENC):
                    continue
                if (mn, cand) in ENC:
                    mode = cand
                    break
            if mode is None:
                raise SystemExit('cannot encode: ' + ln)
            op = ENC[(mn, mode)]
            out[pc] = op
            if mode == 'rel':
                d = (v - (pc + 2)) if pas == 2 else 0
                if pas == 2 and not -128 <= d <= 127:
                    raise SystemExit('branch out of range: ' + ln)
                out[pc + 1] = d & 0xFF
                pc += 2
            elif mode in ('imm', 'zp', 'zpx', 'zpy', 'izx', 'izy'):
                out[pc + 1] = v & 0xFF
                pc += 2
            elif mode in ('abs', 'abx', 'aby', 'ind'):
                out[pc + 1] = v & 0xFF; out[pc + 2] = (v >> 8) & 0xFF
                pc += 3
            else:
                pc += 1
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs', 'battlecity.asm')
    import rom
    prg = rom.prg()
    out = assemble(open(path).read().splitlines())
    bad = [a for a in range(0xC000, 0x10000) if out.get(a) != prg[a - 0xC000]]
    if bad:
        print('MISMATCH at %d bytes, first: %s' % (len(bad), ', '.join('%04X' % a for a in bad[:10])))
        sys.exit(1)
    print('OK: listing reassembles to the ROM PRG (16384 bytes identical)')


if __name__ == '__main__':
    main()
