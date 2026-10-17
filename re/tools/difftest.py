"""Run build/difftest for routines, picking output registers from
docs/contracts.json and the first movie that reaches each routine.

usage: python3 tools/difftest.py [ADDR ...] [--all] [--lifted] [--mod NAME] [--cases N] [--fuzz M] [-j J]
  --lifted   test every address in src/lifted/*.lifted
  --mod NAME use build/NAME/difftest and also test work/NAME/*.lifted
  --all      test every subroutine entry
Writes evidence/difftest_<label>.json and prints a summary.
"""
import concurrent.futures as cf
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rom
ROM = rom.rom_path()
# routines that cannot be run in isolation
UNTESTABLE = {
    0xECD0: 'reads its caller\'s return address (inline jump table after JSR)',
    0xC070: 'reset: never returns',
    0xD400: 'NMI handler: ends in RTI',
    0xE972: 'powerup_pickup: on a pickup it pops its caller\'s return address (pla/pla) and tail-jumps; lockstep covers it',
}

# Routines no movie reaches, tested from the entry states of a routine
# entered in the same context: addr -> (capture address, why)
SURROGATE = {
    0xEC21: (0xEC33, 'sound cmd $E9: no script uses it; $EC33 (cmd $EA) is entered from the same $ECD0 dispatch with the same voice/script context'),
    0xEC45: (0xEC33, 'sound cmd $EB: as $EC21'),
    0xEC57: (0xEC6B, 'sound cmd $EC: no script uses it; $EC6B (cmd $EE) has the same shape (one operand stored to a voice byte)'),
    0xEC61: (0xEC6B, 'sound cmd $ED: as $EC57'),
}


def movies():
    ms = sorted(glob.glob(os.path.join(ROOT, 'movies', 'fceux_*.txt'))) + \
        sorted(glob.glob(os.path.join(ROOT, 'movies', 'synthetic_*.txt')))
    out = []
    for m in ms:
        n = 0
        for ln in open(m):
            if ln.strip() and not ln.startswith('#'):
                n += int(ln.split()[0])
        out.append((m, min(n, 40000)))
    return out


BUILD = os.path.join(ROOT, 'build')


def lifted_addrs(d):
    out = []
    for p in sorted(glob.glob(os.path.join(d, '*.lifted'))):
        for ln in open(p):
            ln = ln.split('#')[0].strip()
            if ln:
                out.append(int(ln, 16))
    return out


def run_one(addr, outregs, cases, fuzz):
    if addr in UNTESTABLE:
        return {'addr': '%04X' % addr, 'status': 'untestable', 'why': UNTESTABLE[addr]}
    for m, frames in movies():
        extra = ['--capture-at', '%04X' % SURROGATE[addr][0]] if addr in SURROGATE else []
        p = subprocess.run([os.path.join(BUILD, 'difftest'), ROM, m, str(frames), '%04X' % addr,
                            outregs or '-', '--cases', str(cases), '--fuzz', str(fuzz), '--maxsteps', '300000'] + extra,
                           capture_output=True, text=True)
        last = p.stdout.strip().splitlines()[-1] if p.stdout.strip() else p.stderr
        if 'never reached' in last:
            continue
        r = re.search(r'(\d+) pass, (\d+) fail, (\d+) skipped .*?, (\d+) waited, (\d+) invalid', last)
        res = {'addr': '%04X' % addr, 'movie': os.path.basename(m), 'out': outregs, 'summary': last}
        if addr in SURROGATE:
            res['surrogate'] = '$%04X: %s' % SURROGATE[addr]
        if r:
            ps, fl, sk, wt, inv = map(int, r.groups())
            res.update(passed=ps, failed=fl, skipped=sk, waited=wt, invalid=inv)
            res['status'] = 'FAIL' if fl else ('pass' if ps else 'no-cases')
        else:
            res['status'] = 'error'
        if res['status'] == 'FAIL':
            res['details'] = p.stdout.strip().splitlines()[:5]
        return res
    return {'addr': '%04X' % addr, 'status': 'unreached', 'why': 'no movie reaches it'}


def main():
    global BUILD
    args = sys.argv[1:]
    mod = args[args.index('--mod') + 1] if '--mod' in args else None
    if mod:
        BUILD = os.path.join(ROOT, 'build', mod)
    cases = int(args[args.index('--cases') + 1]) if '--cases' in args else 100
    fuzz = int(args[args.index('--fuzz') + 1]) if '--fuzz' in args else 4
    jobs = int(args[args.index('-j') + 1]) if '-j' in args else os.cpu_count()
    contracts = json.load(open(os.path.join(ROOT, 'docs', 'contracts.json')))
    if '--all' in args:
        addrs = [int(k, 16) for k in contracts]
        label = 'all'
    elif '--lifted' in args or mod:
        addrs = lifted_addrs(os.path.join(ROOT, 'src', 'lifted'))
        if mod:
            addrs = lifted_addrs(os.path.join(ROOT, 'work', mod))
        label = mod or 'lifted'
        addrs += [int(a, 16) for a in args if re.fullmatch(r'[0-9A-Fa-f]{4}', a)]
    else:
        addrs = [int(a, 16) for a in args if re.fullmatch(r'[0-9A-Fa-f]{4}', a)]
        label = 'adhoc'
    with cf.ThreadPoolExecutor(jobs) as ex:
        futs = [ex.submit(run_one, a, contracts.get('%04X' % a, {}).get('out', 'AXYCZNV'), cases, fuzz) for a in addrs]
        results = [f.result() for f in futs]
    os.makedirs(os.path.join(ROOT, 'evidence'), exist_ok=True)
    json.dump(results, open(os.path.join(ROOT, 'evidence', 'difftest_%s.json' % label), 'w'), indent=1)
    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
        if r['status'] in ('FAIL', 'error'):
            print('%s %s %s' % (r['addr'], r['status'], r.get('summary', '')))
            for d in r.get('details', []):
                print('    ' + d)
    print('summary:', counts)
    sys.exit(1 if counts.get('FAIL') or counts.get('error') else 0)


if __name__ == '__main__':
    main()
