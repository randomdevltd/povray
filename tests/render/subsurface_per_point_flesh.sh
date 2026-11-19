#!/bin/sh
# Each point of a subsurface body takes the flesh of its own brick, and the body keeps one set of cells whichever
# brick's material is shaded.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
render() {
    arm=$1
    shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_per_point_flesh.pov" +w4 +h1 +wt"${THREADS:-1}" +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$arm.ppm" "$@" >"$arm.log" 2>&1 || { cat "$arm.log"; exit 1; }
}
points() { sed -n 's/.* \([0-9]*\) cells, \([0-9]*\) points built.*/\1 \2/p' "$1.log"; }
render union Declare=Layout=0
render copies Declare=Layout=1
render same Declare=Layout=2 Declare=LeftFlesh=0.8
render dark Declare=Layout=2 Declare=LeftFlesh=0.05
render skin Declare=Layout=3
render lightskin Declare=Layout=4 Declare=LeftSkin=0.9
render darkskin Declare=Layout=4 Declare=LeftSkin=0.1
THREADS=4 render parallel Declare=Layout=4 Declare=LeftSkin=0.1
test -n "$(points union)" && test -n "$(points skin)" || { cat union.log skin.log; exit 1; }
# a darker flesh shortens the diffusion, so its cells are another size; a skin leaves it alone
for arm in copies same; do
    test "$(points $arm)" = "$(points union)" || { echo "$arm built $(points $arm) cells and points, one texture $(points union)"; exit 1; }
done
for arm in lightskin darkskin parallel; do
    test "$(points $arm)" = "$(points skin)" || { echo "$arm built $(points $arm) cells and points, one texture $(points skin)"; exit 1; }
done
image_awk '
BEGIN {
    n = read_ppm("union.ppm", union)
    read_ppm("copies.ppm", copies)
    read_ppm("same.ppm", same)
    read_ppm("dark.ppm", dark)
    read_ppm("skin.ppm", skin)
    read_ppm("lightskin.ppm", lightskin)
    read_ppm("darkskin.ppm", darkskin)
    read_ppm("parallel.ppm", parallel)
    if (n != 12 || union[6] <= 0) fail("expected four lit RGB pixels")
    for (i = 0; i < n; ++i) {
        if (copies[i] != union[i]) fail("a copy of the texture on each brick changed a pixel")
        if (same[i] != union[i]) fail("an equal flesh on each brick changed a pixel")
        if (lightskin[i] != skin[i]) fail("an equal skin on each brick changed a pixel")
        if (parallel[i] != darkskin[i]) fail("worker count changed the output")
    }
    for (i = 6; i < 9; ++i) {
        if (!(dark[i] < same[i])) fail("light entering the dark brick kept the next brick'"'"'s flesh")
        if (!(darkskin[i] < lightskin[i])) fail("light entering the dark brick kept the next brick'"'"'s skin")
    }
}'
