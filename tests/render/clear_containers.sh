#!/bin/sh
# clear_containers.sh <povray> <srcdir>: skipping the lights of surfaces they cannot brighten changes no pixel,
# and skips their shadow rays; under +AM4 the sampler's input changes on purpose (no grain marks from zero terms).
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=96; H=72
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/clear_containers.pov" +w$W +h$H -d -p +v -gp +fp16 +wt1 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
same() {
    cmp -s "$1.pixels" "$2.pixels" || { echo "clear_containers: $1 differs from $2" >&2; exit 1; }
}
shadows() {
    awk '$1 == "Shadow" && $2 == "Ray" && $3 == "Tests:" { print $4; exit }' "$1.log"
}
render skip -a
render loop -a Declare=Force=1
same skip loop
a=$(shadows skip); b=$(shadows loop)
[ -n "$a" ] && [ -n "$b" ] && [ $(( 20 * a )) -lt $(( 19 * b )) ] ||
    { echo "clear_containers: $a shadow rays when skipping, $b when not; nothing was skipped" >&2; exit 1; }
# method 4 reads the grain flag, and the skipped loop no longer marks grain from draws that shaped a zero,
# so its sample pattern may differ; both arms must render.
render skip_aa +pr +am4 +a0.02 +ab0.5
render loop_aa +pr +am4 +a0.02 +ab0.5 Declare=Force=1
echo "clear_containers: lights that add nothing are skipped without changing a pixel"
