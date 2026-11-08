#!/bin/sh
# Every host shape that can carry method 2 subsurface finds the exits of paths from a buried volume light, at two scene
# scales whose images must agree: thinness is width against the transport length, never a scene-unit size. One seed of the
# internal transport at this sample count varies by about 5% between scales, so 10% separates noise from a scale fault.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
for case in 0 1 2 3 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19; do
    for scale in 1 1000; do
        "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_hosts.pov" +L"$SRCDIR/distribution/include" +w40 +h30 +wt2 +pr -a -d -p -v -gp \
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
    if (abs(second / first - 1) > 0.10) fail("case " ENVIRON["CASE"] " changes with scene scale: " second / first)
}'
done
# An open mesh whose inside_vector claims the light cannot enclose it: the parse stops and names the mesh. One that
# does not claim it, or a disc whose box merely meets the light's, hosts nothing and renders, with or without
# subsurface in global_settings alike.
if "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_hosts.pov" +w4 +h3 -a -d -p -v -gp -f Declare=Case=20 >open.log 2>&1; then
    echo "case 20 (open mesh claiming the light) rendered instead of failing"; cat open.log; exit 1
fi
grep -q "lies within the subsurface mesh at line .* a mesh with an open border" open.log ||
    { echo "case 20 (open mesh claiming the light) failed without naming the mesh"; cat open.log; exit 1; }
for sss in 1 0; do
    if "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_hosts.pov" +w4 +h3 -a -d -p -v -gp -f Declare=Case=22 Declare=Sss=$sss \
        >disc.log 2>&1; then echo "case 22 (light block on a disc, Sss=$sss) rendered instead of failing"; cat disc.log; exit 1; fi
    grep -q "is on the disc at line .* which encloses no volume to emit from" disc.log ||
        { echo "case 22 (light block on a disc, Sss=$sss) failed without naming the disc"; cat disc.log; exit 1; }
done
for case in 4 21; do
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_hosts.pov" +w4 +h3 -a -d -p -v -gp -f Declare=Case=$case >open.log 2>&1 ||
        { echo "case $case (open part not holding the light) failed"; cat open.log; exit 1; }
done
