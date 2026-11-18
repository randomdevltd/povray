#!/bin/sh
# A volume light just under the skin of each host shape lights it the same at scene scales 0.01, 1 and 1000 (at 0.001
# the camera stands nearer the hosts than MIN_ISECT_DEPTH): paths from the lamp start a hair inside the surface, where
# the host's solver must report the crossing just ahead at full precision, or the exit search stops the render. One
# seed of the internal transport at this sample count varies by about 5% between scales, so 10% separates noise from
# a scale fault.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
for case in 0 1 2 3 4 5 6 7 8; do
    for scale in 0.01 1 1000; do
        "$POVRAY" +i"$SRCDIR/tests/render/buried_walk_roots.pov" +w40 +h30 +wt2 +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
            Stochastic_Seed=0 Declare=Case=$case Declare=S=$scale +o"skin$scale.ppm" >"skin$scale.log" 2>&1 ||
            { echo "case $case at scale $scale failed"; cat "skin$scale.log"; exit 1; }
    done
    export CASE=$case
    image_awk '
BEGIN {
    n1 = read_ppm("skin1.ppm", a); n2 = read_ppm("skin0.01.ppm", b); n3 = read_ppm("skin1000.ppm", c)
    if (n1 != n2 || n1 != n3 || n1 == 0) fail("skin images differ in size")
    first = sum_of(a, n1); small = sum_of(b, n2); large = sum_of(c, n3)
    if (!(first > 0)) fail("case " ENVIRON["CASE"] " rendered black")
    if (abs(small / first - 1) > 0.10 || abs(large / first - 1) > 0.10)
        fail("case " ENVIRON["CASE"] " changes with scene scale: " small / first ", " large / first)
}'
done
