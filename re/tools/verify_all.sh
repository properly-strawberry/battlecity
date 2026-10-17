#!/bin/sh
# Run every verification over every movie in re/movies/.
#  1. listing reassembles to the PRG byte for byte
#  2. reference emulator vs FCEUX: per-frame RAM (stack page $0160-$017F
#     excluded: it holds the interrupted PC, which depends on sub-instruction
#     timing) -- reports frames that differ; a lasting divergence fails
#  3. original vs C reconstruction lockstep at every NMI (must be identical)
# usage: tools/verify_all.sh [FRAMES]        (official build)
#        MOD=name tools/verify_all.sh [FRAMES]   (work package build/name)
set -e
cd "$(dirname "$0")/.."
ROM="${BC_ROM:-../BattleCity (Japan) (En).nes}"   # same default as tools/rom.py
FRAMES=${1:-20000}
TMP=${TMPDIR:-/tmp}/bc_verify.$$
mkdir -p "$TMP"
if [ -n "$MOD" ]; then
    make -s agent MOD="$MOD" build/nesrun
    B=build/$MOD
else
    make -s build/lockstep build/nesrun
    B=build
fi
python3 tools/asm_check.py
status=0
for m in movies/*.txt; do
    name=$(basename "$m" .txt)
    # movie length in frames (bounded by FRAMES)
    len=$(awk '!/^#/ && NF {n += $1} END {print n}' "$m")
    F=$FRAMES; [ "$len" -gt 0 ] && [ "$len" -lt "$F" ] && F=$len
    if [ -z "$MOD" ] && [ -x third_party/fceux_headless/fceux_headless ]; then
        third_party/fceux_headless/fceux_headless "$ROM" "$m" "$F" "$TMP/fx.bin" 2>/dev/null >/dev/null
        build/nesrun "$ROM" "$m" "$F" --ram "$TMP/my.bin" 2>/dev/null
        python3 tools/oracle_check.py "$TMP/fx.bin" "$TMP/my.bin" "$name" || status=1
    fi
    printf '%-24s lockstep: ' "$name"
    $B/lockstep "$ROM" "$m" "$F" | tail -1 || status=1
done
rm -rf "$TMP"
exit $status
