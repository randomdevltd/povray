#!/bin/sh
# bounce_adaptive.sh <povray> <srcdir>: adaptive surface bounce nodes refine alike on one thread or several and at a
# thousandth or a thousand times the scale, and a plain box room bounces as its inverse does.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=8; H=6
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_adaptive.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" Declare=Samples=4 "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
for case in 0 1 2 3 4 5; do
    render one$case Declare=Case=$case +wt1
    render four$case Declare=Case=$case +wt4
    cmp -s one$case.pixels four$case.pixels || { echo "bounce_adaptive: case $case differs on four threads" >&2; exit 1; }
    grep 'Bounce o1:' one$case.log | sed 's/, [0-9.]*+[0-9.]*s//' > one$case.nodes
    grep 'Bounce o1:' four$case.log | sed 's/, [0-9.]*+[0-9.]*s//' > four$case.nodes
    [ -s one$case.nodes ] && cmp -s one$case.nodes four$case.nodes ||
        { cat one$case.nodes four$case.nodes; echo "bounce_adaptive: case $case refines differently" >&2; exit 1; }
done
render passes2 Declare=Case=2 Declare=Passes=2 +wt3
grep -q 'Bounce o2:' passes2.log || { cat passes2.log; echo "bounce_adaptive: no second order" >&2; exit 1; }
# Means of the image's four quadrants, then the order's node count.
blocks() {
    od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk -v w=$W -v h=$H '{ i = NR - 1; b = (int(i % w * 2 / w)) + 2 * int(int(i / w) * 2 / h)
        s[b] += $1 + $2 + $3; n[b] += 3 } END { for (b = 0; b < 4; b++) printf "%.6f ", s[b] / n[b] / 65535; print "" }'
}
nodes() { sed -n 's/.*Bounce o1: \([0-9]*\) nodes.*/\1/p' "$1.log"; }
# a b tolerance [nodes]: every block of b within the relative tolerance of a's, and with `nodes` node counts within 3%.
alike() {
    paste -d' ' "$1.blocks" "$2.blocks" | awk -v t=$3 -v na=${4:+$(nodes $1)} -v nb=$(nodes $2) '{ for (i = 1; i <= 4; i++)
        if ($i - $(i + 4) > t * $i + 1e-3 || $(i + 4) - $i > t * $i + 1e-3) bad = 1 }
        END { if (na != "" && (nb - na > 0.03 * na || na - nb > 0.03 * na)) bad = 1; exit bad }'
}
W=48; H=36
render room6 Declare=Case=6 Declare=Samples=16 +wt3
render inverse6 Declare=Case=6 Declare=Samples=16 Declare=Inverse=1 +wt3
blocks room6 > room6.blocks; blocks inverse6 > inverse6.blocks
W=8; H=6
alike room6 inverse6 0.08 || { cat room6.blocks inverse6.blocks
    echo "bounce_adaptive: a plain box room bounces unlike its inverse" >&2; exit 1; }
for case in 0 4; do
    for s in 1 0.001 1000; do
        W=64; H=48; render scale$case-$s Declare=Case=$case Declare=S=$s Declare=Samples=16 +wt3
        blocks scale$case-$s > scale$case-$s.blocks
    done
    for s in 0.001 1000; do
        alike scale$case-1 scale$case-$s 0.08 nodes || { cat scale$case-1.blocks scale$case-$s.blocks; nodes scale$case-1; nodes scale$case-$s
            echo "bounce_adaptive: case $case refines or renders differently at scale $s" >&2; exit 1; }
    done
done
echo "bounce_adaptive: adaptive bounce nodes are the same on any thread count and at a thousandth or a thousand times the scale"
