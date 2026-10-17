#!/bin/sh
# Sound checks for the game's APU (src/platform/apu.c). For each user movie:
#  1. APU vs FCEUX: channel state at the end of every frame (periods, length
#     counters, envelopes, triangle linear counter) from our APU fed by the
#     original's register writes at their CPU cycle (--fceux-timing) vs
#     FCEUX 2.6.6's own state. Pass/fail: tools/apu_state_compare.py.
#  2. Game vs original (report): the SDL3 game's sound (headless, movie
#     input; APU writes at fixed points of the frame, see main_sdl.c) vs the
#     same APU fed at the original's exact cycles, tones only (noise is
#     random and cannot be compared by waveform). Per-frame correlation with
#     up to half a frame of lag.
# usage: tools/check_audio.sh        Outputs go to build/apu/.
cd "$(dirname "$0")/.." || exit 2
ROM="${BC_ROM:-../BattleCity (Japan) (En).nes}"
FX=third_party/fceux_headless/fceux_headless
make -s game build/nesrun build/apucheck build/audiocmp || exit 2
mkdir -p build/apu
status=0
for m in movies/fceux_*.txt; do
    name=$(basename "$m" .txt)
    len=$(awk '!/^#/ && NF {n += $1} END {print n}' "$m")
    last=$((len - 1))
    build/apucheck "$ROM" "$m" "$len" "build/apu/$name.fx" --fceux-timing --state 0 "$last" > "build/apu/$name.ours.state"
    "$FX" "$ROM" "$m" "$len" build/apu/tmp.ram --apustate 0 "$last" 2>/dev/null > "build/apu/$name.fceux.state"
    if python3 tools/apu_state_compare.py "build/apu/$name.ours.state" "build/apu/$name.fceux.state" > "build/apu/$name.state.txt"; then r=PASS; else r=FAIL; status=1; fi
    worst=$(grep "% match" "build/apu/$name.state.txt" | awk '{print $2}' | sort -n | head -1)
    printf '%-24s apu vs FCEUX state: %s (worst field %s)\n' "$name" "$r" "$worst"

    build/nesrun "$ROM" "$m" "$len" --nmipc "build/apu/$name.nmi" >/dev/null 2>&1
    nmis=$(awk '$2 != "0000"' "build/apu/$name.nmi" | wc -l)
    build/battlecity "$ROM" --headless --nmis "$nmis" --movie "$m" --nmimap "build/apu/$name.nmi" \
        --audio "build/apu/$name.game" --apumute 8
    build/apucheck "$ROM" "$m" "$len" "build/apu/$name.orig" --mute 8
    printf '%-24s game vs original tones: ' "$name"
    build/audiocmp "build/apu/$name.orig" "build/apu/$name.game" --map "build/apu/$name.game.map" --lag 390 | tail -1
done
exit $status
