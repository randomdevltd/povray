#!/bin/sh
# Torus, surface of revolution and ovus solved their ray equations in object units against absolute thresholds, so a
# solid modelled a few hundredths of a unit across lost crossings (and a small surface of revolution its end caps)
# however it was scaled afterwards. The scene checks trace() against inside() with the same solids modelled in four units,
# for missed, late and early (phantom) hits.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for unit in 1 0.01 0.0001 1000; do
    "$POVRAY" +i"$SRCDIR/tests/render/revolved_solid_roots.pov" +w4 +h3 -a -d -p -v -gp -f Declare=U=$unit >check.log 2>&1 ||
        { echo "ray check in units of $unit failed"; cat check.log; exit 1; }
done
