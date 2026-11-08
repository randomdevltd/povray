#!/bin/sh
# shadow_walk.sh <povray> <srcdir>: the one-pass shadow walk (bounding on) shades every case as the restarting walk does
# (bounding off), on one thread or four, and reports the walks and crossings it made.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/shadow_walk_cases.pov" +w$W +h$H -a -d -p -gp +fp16 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; echo "shadow_walk: $name failed" >&2; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
# Case 7 holds a doubled box, which the eye sees as either copy depending on the bounding, and case 15 tests what a thread's
# cached blocker may skip, so both are checked across threads only.
for case in 1 2 4 5 6 7 12 13 15; do
    # The parametric torus is slow to intersect, so its case renders smaller.
    if [ $case = 12 ]; then W=24; H=18; else W=48; H=36; fi
    render walk$case Declare=Case=$case +wt1 +mb
    render walk${case}_4 Declare=Case=$case +wt4 +mb
    cmp -s walk$case.pixels walk${case}_4.pixels || { echo "shadow_walk: case $case differs on 4 threads" >&2; exit 1; }
    grep -q "Shadow Walks:" walk$case.log || { echo "shadow_walk: case $case reports no shadow walks" >&2; exit 1; }
    case $case in 7|15) continue ;; esac
    render restart$case Declare=Case=$case +wt1 -mb
    cmp -s walk$case.pixels restart$case.pixels || { echo "shadow_walk: case $case one-pass walk differs from the restarting walk" >&2; exit 1; }
done
echo "shadow_walk: the one-pass walk matches the restarting walk on opaque, patterned, isosurface, CSG, no_shadow, parametric and nearest-root shapes, and renders the same on 1 and 4 threads"
