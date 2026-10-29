#!/bin/sh
# bounce_adaptive.sh <povray> <srcdir>: adaptive surface bounce nodes refine alike on one thread or several.
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
    grep 'Surface bounce o1:' one$case.log | sed 's/, [0-9.]*s+[0-9.]*s//' > one$case.nodes
    grep 'Surface bounce o1:' four$case.log | sed 's/, [0-9.]*s+[0-9.]*s//' > four$case.nodes
    [ -s one$case.nodes ] && cmp -s one$case.nodes four$case.nodes ||
        { cat one$case.nodes four$case.nodes; echo "bounce_adaptive: case $case refines differently" >&2; exit 1; }
done
render passes2 Declare=Case=2 Declare=Passes=2 +wt3
grep -q 'Surface bounce o2:' passes2.log || { cat passes2.log; echo "bounce_adaptive: no second order" >&2; exit 1; }
echo "bounce_adaptive: adaptive bounce nodes are the same on any thread count"
