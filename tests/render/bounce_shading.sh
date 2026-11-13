#!/bin/sh
# bounce_shading.sh <povray> <srcdir>: a bounce light exports what the shading call sees: finishes refused before render,
# light crosses a contact between two translucent slabs of one bounce union, and none leaks through opaque ones.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=16; H=10
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_shading.pov" +w$W +h$H +wt2 +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" Declare=Cells=512 "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
sum() { od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk '{ s += $1 + $2 + $3 } END { print s + 0 }'; }
render fresnel Declare=Case=2
render crand Declare=Case=1 Declare=Crand=0.3
[ "$(sum fresnel)" -gt 0 ] && [ "$(sum crand)" -gt 0 ] || { echo "bounce_shading: a Fresnel or crand bounce wall is black" >&2; exit 1; }
render crossed Declare=Case=3
render apart Declare=Case=4
render opaque Declare=Case=3 Declare=Opaque=1
echo "$(sum crossed) $(sum apart) $(sum opaque)" | awk '{ if (!($1 > 10 * $2) || $3 != 0) exit 1 }' ||
    { echo "bounce_shading: crossed $(sum crossed), apart $(sum apart), opaque $(sum opaque): light must cross the contact and only there" >&2; exit 1; }
echo "bounce_shading: passed"
