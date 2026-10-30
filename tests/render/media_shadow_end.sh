#!/bin/sh
# media_shadow_end.sh <povray> <srcdir>: a light's shadow ray is dimmed by every absorbing medium on its path, wherever the light sits.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=16; H=12
render() {
    out=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_shadow_end.pov" +w$W +h$H -a -d -p -v -gp +fp16 +wt2 File_Gamma=1.0 +o"$out.ppm" "$@" \
        > "$out.log" 2>&1 || { cat "$out.log"; exit 1; }
}
mean() {
    tail -c $(( 6 * W * H )) "$1.ppm" | od -An -v -tu1 -w6 | awk '{ s += ($1 * 256 + $2) / 65535; n++ } END { printf "%.6f", s / n }'
}
# optical depths in units of 0.1 along the light's path and the camera's, per case
light_depth() { echo 2 3 3 4 2.5 | cut -d' ' -f$1; }
camera_depth() { echo 2 4 3 5 3 | cut -d' ' -f$1; }
check() {
    case=$1; method=$2; block=$3; shift 3
    name=c${case}_m${method}_b$block
    render ${name}_clear Declare=Case=$case Declare=Method=$method Declare=Block=$block Declare=Haze=0 "$@"
    render $name Declare=Case=$case Declare=Method=$method Declare=Block=$block "$@"
    clear=$(mean ${name}_clear); hazy=$(mean $name)
    awk -v c="$clear" -v h="$hazy" -v t="$(light_depth $case)" -v e="$(camera_depth $case)" -v n=$name 'BEGIN {
        want = exp(-0.1 * (t + e)); got = h / c
        if (c < 0.05 || got - want > 0.003 || want - got > 0.003) {
            printf "media_shadow_end: %s transmits %.5f, expected %.5f\n", n, got, want > "/dev/stderr"; exit 1 } }'
}
for case in 1 2 3 4 5; do
    for block in 0 1; do check $case 4 $block; done
done
check 4 4 0 Declare=Scale=0.001
check 4 4 0 Declare=Scale=1000
echo "media_shadow_end: shadow rays carry the media around both ends of the path"
