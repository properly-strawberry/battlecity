#!/bin/sh
# Check the SDL3 game build (build/battlecity) against the original:
# replay each user-recorded movie through the game, headless, and compare
# the picture after several NMIs with the reference emulator's picture of
# the frame in which the original took that NMI. Must be pixel-identical.
#
# usage: tools/check_game.sh [SHOTS_PER_MOVIE]   (default 6)
# Needs SDL3 (pkg-config sdl3) for `make game`. Outputs go to build/shots/.
cd "$(dirname "$0")/.." || exit 2
ROM="${BC_ROM:-../BattleCity (Japan) (En).nes}"
K=${1:-6}
make -s game build/nesrun || exit 2
mkdir -p build/shots
status=0
for m in movies/fceux_*.txt; do
    name=$(basename "$m" .txt)
    len=$(awk '!/^#/ && NF {n += $1} END {print n}' "$m")
    build/nesrun "$ROM" "$m" "$len" --nmipc "build/shots/$name.nmi" >/dev/null 2>&1
    nmis=$(awk '$2 != "0000"' "build/shots/$name.nmi" | wc -l)
    # K NMIs spread over the run; reference frame = the frame of that NMI
    pairs=$(awk -v k="$K" -v n="$nmis" '$2 != "0000" { i++; f[i] = $1 }
        END { for (j = 1; j <= k; j++) { x = int(n * j / (k + 1)); if (x > 0) print x, f[x] } }' "build/shots/$name.nmi")
    a=""; b=""
    for x in $(echo "$pairs" | awk '{print $1 ":" $2}'); do
        nmi=${x%%:*}; fr=${x##*:}
        a="$a --shot $nmi:build/shots/${name}_game_$nmi.ppm"
        b="$b --shot $fr:build/shots/${name}_ref_$nmi.ppm"
    done
    build/battlecity "$ROM" --headless --nmis "$nmis" --movie "$m" --nmimap "build/shots/$name.nmi" $a || status=1
    build/nesrun "$ROM" "$m" "$len" $b >/dev/null 2>&1
    same=0; diff=0
    for nmi in $(echo "$pairs" | awk '{print $1}'); do
        if cmp -s "build/shots/${name}_game_$nmi.ppm" "build/shots/${name}_ref_$nmi.ppm"; then same=$((same + 1))
        else diff=$((diff + 1)); echo "  $name: picture after NMI $nmi differs"; fi
    done
    printf '%-24s game: %d/%d pictures identical (%d NMIs)\n' "$name" "$same" "$((same + diff))" "$nmis"
    [ "$diff" -eq 0 ] || status=1
done
exit $status
