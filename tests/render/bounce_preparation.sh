#!/bin/sh
# bounce_preparation.sh <povray> <srcdir>: bounce preparation on one thread or several gives the same image.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=8; H=8
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_preparation.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" Declare=Sources=3 Declare=Samples=2 "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
for case in 0 1 2 3 4 5 6 7; do
    render one$case Declare=Case=$case +wt1
    render four$case Declare=Case=$case +wt4
    cmp -s one$case.pixels four$case.pixels || { echo "bounce_preparation: case $case differs on four threads" >&2; exit 1; }
done
grep -q 'Info: Preparation: bounce finished [0-9]*\.[0-9]s$' one0.log ||
    { cat one0.log; echo "bounce_preparation: no bounce progress line" >&2; exit 1; }
# A light on a node and a millionth off it light the node's cell alike: every channel's image sum within 0.1%.
render off7 Declare=Case=7 Declare=Offset=1e-6 +wt4
sums() { od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk '{ r += $1; g += $2; b += $3 } END { print r, g, b }'; }
echo "$(sums one7) $(sums off7)" | awk '{ for (i = 1; i <= 3; i++) if (!($i > 0) || $i - $(i + 3) > 1e-3 * $i ||
    $(i + 3) - $i > 1e-3 * $i) bad = 1; exit bad }' ||
    { sums one7; sums off7; echo "bounce_preparation: a light on a node lights it unlike one beside it" >&2; exit 1; }
# A bounce-only block's brightness multiplies its bounce: over the unclipped samples, what gain 2 adds over gain 0 is
# twice what gain 1 adds, within 1%.
for gain in 0 1 2; do render gain$gain Declare=Case=7 Declare=Gain=$gain +wt4; done
values() { od -An -v -tu2 --endian=big -w2 "$1.pixels"; }
values gain0 > gain0.values; values gain1 > gain1.values; values gain2 > gain2.values
paste gain0.values gain1.values gain2.values | awk '$3 < 65535 { a += $2 - $1; b += $3 - $1 }
    END { if (!(a > 0) || b - 2 * a > 0.02 * a || 2 * a - b > 0.02 * a) { print "gain 1 adds " a ", gain 2 adds " b; exit 1 } }' ||
    { echo "bounce_preparation: bounce gain does not scale the bounce linearly" >&2; exit 1; }
echo "bounce_preparation: bounce preparation is the same on any thread count"
