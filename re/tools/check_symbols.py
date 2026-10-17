"""Structural check of symbol names against the verified disassembly.

For every named ROM address: is it an instruction start (code names) or
inside a data/jump-table region (table names), and is it a subroutine entry
reached by JSR/vector/jump table? Prints a report; exit 1 on hard conflicts
(a code name on a non-instruction byte)."""
import json
import os
import sys

import symbols

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cm = json.load(open(os.path.join(ROOT, 'docs', 'codemap.json')))
cls = cm['classes']
subs = set(int(a, 16) for a in cm['subs'])
bad = 0
for a, (name, status, note) in sorted(symbols.CODE_DEFS.items()):
    c = cls[a - 0xC000]
    is_table = name.startswith('tbl_') or 'table' in name or name in ('enemy_spawn_x', 'enemy_spawn_y', 'player_spawn_x', 'player_spawn_y')
    if is_table:
        ok = c in 'DJ'
        kind = 'data'
    else:
        ok = c == 'O'
        kind = 'code'
    entry = 'entry' if a in subs else ('label' if c == 'O' else '-')
    flag = 'ok ' if ok else 'BAD'
    if not ok:
        bad += 1
    if not ok or '-v' in sys.argv:
        print('%s $%04X %-28s %s class=%s %s' % (flag, a, name, kind, c, entry))
print('%d names checked, %d conflicts' % (len(symbols.CODE_DEFS), bad))
sys.exit(1 if bad else 0)
