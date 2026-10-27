#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_transport.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render white
render red Declare=Case=4
render absorbed Declare=Sigma=0.7
render shaped Declare=Case=2
render shadow Declare=Case=3 Declare=Draws=1024
render sparse Declare=Draws=64
render scaled Declare=SceneScale=2
render unsplit Declare=Far=0
render distant Declare=Height=20 Declare=Gain=100
render remote Declare=Height=100000 Declare=Gain=100000000
render fog Declare=Case=6 Declare=Sigma=0.7
render atmosphere_box Declare=Case=7 Declare=Sigma=0.7
if [ "${VOLUME_TEST_SUBSET:-all}" = lumens ] || [ "${LUMENS_ONLY:-0}" = 1 ]; then
    python3 "$SRCDIR/tests/render/volume_transport_reference.py" "$WORK" --subset lumens
    exit 0
fi
render cube_center Declare=Case=5 Declare=Probe=0.5 Declare=Draws=1024
render cube_scaled Declare=Case=5 Declare=SceneScale=2 Declare=Draws=1024
render half_box Declare=Case=8 Declare=Draws=1024
render clipped_box Declare=Case=9 Declare=Draws=1024
for gap in 0 0.02 0.1 0.5 1 2 4 8 16; do
    render "cube_$gap" Declare=Case=5 Declare=Gap="$gap" Declare=Draws=1024
done
python3 "$SRCDIR/tests/render/volume_transport_reference.py" "$WORK"
syntax() {
    "$POVRAY" +i"$SRCDIR/tests/render/volume_transport_syntax.pov" +w4 +h4 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Declare=Case="$1" +o"syntax_$1.ppm" > "syntax_$1.log" 2>&1
}
for value in 1 2 3 4 5 6; do
    if syntax "$value"; then
        echo "volume_transport: forbidden default setting $value accepted" >&2
        exit 1
    fi
    grep -q 'accepts only fade_cutoff' "syntax_$value.log"
done
for value in 20 21; do
    if syntax "$value"; then
        echo "volume_transport: removed syntax $value accepted" >&2
        exit 1
    fi
done
for value in 7 11 12 13 14 15 16 22 23 24; do
    syntax "$value" || { cat "syntax_$value.log"; exit 1; }
done
for pair in '22 cluster_radii' '23 far_radii' '24 fade_cutoff'; do
    set -- $pair
    count=$(tr -s ' \n' '  ' < "syntax_$1.log" | grep -o "$2 is" | wc -l)
    [ "$count" -eq 1 ] || { echo "volume_transport: expected one $2 warning, got $count" >&2; exit 1; }
done
for pair in '11 12' '13 14' '15 16'; do
    set -- $pair
    tail -c 96 "syntax_$1.ppm" > syntax_first.pixels
    tail -c 96 "syntax_$2.ppm" > syntax_second.pixels
    cmp syntax_first.pixels syntax_second.pixels
done
echo 'volume_transport: independent quadrature, flux, source media, colour, shadows, scaling and syntax passed'
