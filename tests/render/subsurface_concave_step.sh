#!/bin/sh
# subsurface_concave_step.sh <povray> <srcdir>: a concave dish on a method 2 surface map takes its far points across the
# step the crease test sees, through escape runs traced with the exit point's own ticket, and renders.
set -e
trap 'rm -f ssconcave_*' EXIT
POVRAY=$1; SRCDIR=$2
fail() { echo "subsurface_concave_step: $*"; exit 1; }
"$POVRAY" +i"$SRCDIR/tests/render/subsurface_concave_step.pov" -d -p -a +fp16 +w80 +h60 +wt2 +ossconcave_dish.ppm > ssconcave_dish.log 2>&1 ||
    fail "the dish did not render (exit $?): $(tail -n 3 ssconcave_dish.log)"
tr -d ' \r\n' < ssconcave_dish.log > ssconcave_dish.txt
set -- $(sed -n 's/.*Method2pointcloud:\([0-9]*\)shadingpointstried,\([0-9]*\)served.*withpointsunfoldedacrosscreases\([0-9]*\);.*/\1 \2 \3/p' ssconcave_dish.txt)
[ "$#" -eq 3 ] && [ "$1" -gt 0 ] && [ "$1" -eq "$2" ] || fail "the dish was not served by its map: $*"
[ "$3" -gt 0 ] || fail "no exit point took a far point across the dish's step, so the escape runs went untested"
echo "subsurface_concave_step: a concave dish's far points step through escape runs on the surface map"
