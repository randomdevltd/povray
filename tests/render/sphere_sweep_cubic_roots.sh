#!/bin/sh
# Cubic and b-spline sphere sweeps report every crossing of a ray at any scene scale: the solver worked on absolute
# coefficient sizes, so thin or small tubes lost roots, eye rays passed through them and a buried light's paths found
# no way out. The scene checks trace() against inside() on rays along and across a thin tube, then renders thin
# stalks, a socketed bulb and a b-spline merged with a mesh as hosts of a volume light at two scales that must agree.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
SCENE="$SRCDIR/tests/render/sphere_sweep_cubic_roots.pov"
for scale in 1 0.01 1000; do
    "$POVRAY" +i"$SCENE" +w4 +h3 -a -d -p -v -gp -f Declare=Check=1 Declare=S=$scale >check.log 2>&1 ||
        { echo "ray check at scale $scale failed"; cat check.log; exit 1; }
done
for case in 0 1 2; do
    for scale in 1 1000; do
        "$POVRAY" +i"$SCENE" +w40 +h30 +wt2 +pr -a -d -p -v -gp +fp16 File_Gamma=1 Stochastic_Seed=0 \
            Declare=Case=$case Declare=S=$scale +o"sweep$scale.ppm" >"sweep$scale.log" 2>&1 ||
            { echo "case $case at scale $scale failed"; cat "sweep$scale.log"; exit 1; }
    done
    export CASE=$case
    image_awk '
BEGIN {
    na = read_ppm("sweep1.ppm", a); nb = read_ppm("sweep1000.ppm", b)
    if (na != nb || na == 0) fail("sweep images differ in size")
    first = sum_of(a, na); second = sum_of(b, nb)
    if (!(first > 0)) fail("case " ENVIRON["CASE"] " rendered black")
    if (abs(second / first - 1) > 0.10) fail("case " ENVIRON["CASE"] " changes with scene scale: " second / first)
}'
done
