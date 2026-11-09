#!/bin/sh
# subsurface_translucency_zero.sh <povray> <srcdir>: a translucency channel of 0 has no subsurface transport. A buried
# emitter's glow is exactly 0 in it and unchanged in the others; under an outside light that channel shades as plain
# diffuse. A negative or non-finite channel is a parse error.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=32; H=24
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_translucency_zero.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
sums() { od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk '{ r += $1; g += $2; b += $3 } END { print r, g, b }'; }
render zero
render one "Declare=T=1"
echo "$(sums zero) $(sums one)" | awk '{
    if (!($1 > 0 && $3 > 0)) { print "subsurface_translucency_zero: the carried channels are dark"; exit 1 }
    if ($2 != 0) { print "subsurface_translucency_zero: a channel of translucency 0 glows: " $2; exit 1 }
    if ($1 != $4 || $3 != $6) { print "subsurface_translucency_zero: the carried channels changed: " $1 " " $3 " vs " $4 " " $6; exit 1 } }'
render lit Declare=Ext=1
render plain Declare=Ext=1 Declare=Plain=1
echo "$(sums lit) $(sums plain)" | awk 'function abs(v) { return v < 0 ? -v : v }
    { if (!($5 > 0) || abs($2 - $5) > 0.001 * $5) { print "subsurface_translucency_zero: the channel is not plain diffuse: " $2 " vs " $5; exit 1 } }'
if "$POVRAY" +i"$SRCDIR/tests/render/subsurface_translucency_zero.pov" +w8 +h6 -a -d -p -v -gp Declare=Bad=1 -o- \
    > bad.log 2>&1; then echo "subsurface_translucency_zero: a negative translucency parsed" >&2; exit 1; fi
grep -q "has green channel -1" bad.log ||
    { cat bad.log; echo "subsurface_translucency_zero: the error does not name the finish and channel" >&2; exit 1; }
