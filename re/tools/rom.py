"""Load the original ROM for the analysis tools (Python twin of src/rom.c).

Every tool reads PRG/CHR through here, from the user's .nes file. Nothing in
the project embeds game data. ROM path, in order: BC_ROM environment
variable, then the project's copy next to re/ ("BattleCity (Japan) (En).nes").
The PRG/CHR hashes must match the dump this reconstruction is based on.

  python3 tools/rom.py             check the ROM, print hashes
  python3 tools/rom.py --extract   write rom/prg.bin and rom/chr.bin
                                   (raw PRG for Ghidra/REA; nothing else reads them)
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ROM = os.path.join(os.path.dirname(ROOT), 'BattleCity (Japan) (En).nes')

PRG_SHA256 = '040c74abfdf70f6eecf410651fc0a143ecab3967e321ff5793ca698aa3009d1e'
CHR_SHA256 = 'a5d5a10b0fbe9b1a8b8806a06af2634d25ed993b3659131d01acdf86de9c00e9'


def rom_path():
    return os.environ.get('BC_ROM') or DEFAULT_ROM


def load(path=None):
    """Return (prg, chr, flags6) or exit with a message."""
    path = path or rom_path()
    try:
        data = open(path, 'rb').read()
    except OSError as e:
        sys.exit('cannot open ROM %s: %s (set BC_ROM to your battlecity.nes)' % (path, e.strerror))
    if data[:4] != b'NES\x1a':
        sys.exit('%s is not an iNES (.nes) file' % path)
    off = 16 + (512 if data[6] & 0x04 else 0)
    prg, chr_ = data[off:off + 0x4000], data[off + 0x4000:off + 0x6000]
    hp, hc = hashlib.sha256(prg).hexdigest(), hashlib.sha256(chr_).hexdigest()
    if len(chr_) != 0x2000 or hp != PRG_SHA256 or hc != CHR_SHA256:
        sys.exit('%s is not the supported Battle City dump\n  PRG sha256 %s (want %s)\n'
                 '  CHR sha256 %s (want %s)' % (path, hp, PRG_SHA256, hc, CHR_SHA256))
    return prg, chr_, data[6]


_cache = None


def prg():
    """PRG bytes ($C000-$FFFF), loaded once per process."""
    global _cache
    if _cache is None:
        _cache = load()
    return _cache[0]


if __name__ == '__main__':
    p, c, _ = load()
    print('OK: %s\n  PRG sha256 %s\n  CHR sha256 %s' % (rom_path(), PRG_SHA256, CHR_SHA256))
    if '--extract' in sys.argv:
        os.makedirs(os.path.join(ROOT, 'rom'), exist_ok=True)
        open(os.path.join(ROOT, 'rom', 'prg.bin'), 'wb').write(p)
        open(os.path.join(ROOT, 'rom', 'chr.bin'), 'wb').write(c)
        print('wrote rom/prg.bin, rom/chr.bin')
