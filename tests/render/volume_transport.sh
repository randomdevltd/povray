#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
reference() {
    image_awk -l volume_transport_reference 'BEGIN { volume_transport_check(ARGV[1], ARGV[2]) }' "$WORK" "$1"
}
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_transport.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
check_photons() {
    render photon_gathered Declare=PhotonGate=1 Declare=PhotonQuality=16 Declare=Draws=512 Stochastic_Seed=0
    for pattern in 'samples/set), [1-9][0-9]* deposits' ' [1-9][0-9]* contributions;'; do
        tr '\n' ' ' < photon_gathered.log | tr -s ' ' | grep -q "$pattern" ||
            { cat photon_gathered.log; exit 1; }
    done
    reference photons
}
if [ "${VOLUME_TEST_SUBSET:-all}" = photons ]; then
    check_photons
    exit 0
fi
render white
render red Declare=Case=4
render absorbed Declare=Sigma=0.7
render shaped Declare=Case=2
render shadow Declare=Case=3 Declare=Draws=1024
render sparse Declare=Draws=64
render scaled Declare=SceneScale=2
render distant Declare=Height=20 Declare=Gain=100
render remote Declare=Height=100000 Declare=Gain=100000000
render fog Declare=Case=6 Declare=Sigma=0.7
render atmosphere_box Declare=Case=7 Declare=Sigma=0.7
if [ "${VOLUME_TEST_SUBSET:-all}" = lumens ] || [ "${LUMENS_ONLY:-0}" = 1 ]; then
    reference lumens
    exit 0
fi
render cube_center Declare=Case=5 Declare=Probe=0.5 Declare=Draws=1024
render cube_scaled Declare=Case=5 Declare=SceneScale=2 Declare=Draws=1024
render half_box Declare=Case=8 Declare=Draws=1024
render clipped_box Declare=Case=9 Declare=Draws=1024
for gap in 0 0.02 0.1 0.5 1 2 4 8 16; do
    render "cube_$gap" Declare=Case=5 Declare=Gap="$gap" Declare=Draws=1024
done
reference all
check_photons
syntax() {
    "$POVRAY" +i"$SRCDIR/tests/render/volume_transport_syntax.pov" +w4 +h4 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Declare=Case="$1" +o"syntax_$1.ppm" > "syntax_$1.log" 2>&1
}
for value in 1 2 3 4 5 6 7 20 21 22 23 24; do
    if syntax "$value"; then
        echo "volume_transport: invalid context $value accepted" >&2; exit 1
    fi
done
for value in 11 12 13 14 15 16 17 25 26; do
    syntax "$value" || { cat "syntax_$value.log"; exit 1; }
done
for pair in '11 12' '13 14' '15 16' '15 17'; do
    set -- $pair
    tail -c 96 "syntax_$1.ppm" > syntax_first.pixels
    tail -c 96 "syntax_$2.ppm" > syntax_second.pixels
    cmp syntax_first.pixels syntax_second.pixels
done
echo 'volume_transport: independent quadrature, flux, source media, colour, shadows, scaling and syntax passed'
