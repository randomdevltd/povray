#!/bin/sh
# media_ordinary_scattering.sh <povray> <srcdir>: method 4 media scatter an ordinary light with no media light in the scene.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=32; H=32
# render <name> <options>: renders and keeps the mean channel value, 0 to 65535, in <name>.mean
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_ordinary_scattering.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" | od -An -v -tu1 | awk '{ for (i = 1; i < NF; i += 2) { s += $i * 256 + $(i + 1); n++ } }
        END { printf "%.1f\n", s / n }' > "$name.mean"
}
# Each case: Global, and the mean with scattering at this size.
for case in "0 777.4" "1 8113.3"; do
    set -- $case
    render lit$1 Declare=Global=$1
    render unlit$1 Declare=Global=$1 Declare=Scatter=0
    lit=$(cat lit$1.mean); unlit=$(cat unlit$1.mean)
    awk -v l="$lit" -v u="$unlit" -v e="$2" -v g="$1" 'BEGIN { if (!(l > u + 100) || (l - e) * (l - e) > (0.005 * e) * (0.005 * e)) {
        printf "media_ordinary_scattering: Global=%d mean %s, %s unscattered, expected %s\n", g, l, u, e > "/dev/stderr"; exit 1 } }'
done
echo "media_ordinary_scattering: method 4 fog scatters an ordinary light, contained or global"
