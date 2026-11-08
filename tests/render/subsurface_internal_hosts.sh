#!/bin/sh
# Every host shape that can carry method 2 subsurface finds the exits of paths from a buried volume light, at two scene
# scales whose images must agree: thinness is width against the transport length, never a scene-unit size.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
for case in 0 1 2 3 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19; do
    for scale in 1 1000; do
        "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_hosts.pov" +w20 +h15 +wt2 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Case=$case Declare=S=$scale +o"host$scale.ppm" >"host$scale.log" 2>&1 ||
            { echo "case $case at scale $scale failed"; cat "host$scale.log"; exit 1; }
    done
    export CASE=$case
    image_awk '
BEGIN {
    na = read_ppm("host1.ppm", a); nb = read_ppm("host1000.ppm", b)
    if (na != nb || na == 0) fail("host images differ in size")
    first = sum_of(a, na); second = sum_of(b, nb)
    if (!(first > 0)) fail("case " ENVIRON["CASE"] " rendered black")
    if (abs(second / first - 1) > 0.05) fail("case " ENVIRON["CASE"] " changes with scene scale: " second / first)
}'
done
