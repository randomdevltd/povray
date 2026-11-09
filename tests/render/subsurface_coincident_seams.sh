#!/bin/sh
# Hosts whose surfaces coincide inside CSG (halves cut on one plane, shells sharing a face, equal spheres, a cylinder cap
# on a box face, T-junctions, grazing and nested seams, a roll of offset half-shells) carry a buried volume light under
# method 2: the exit search must leave every seam, and each image must agree at scene scales 1 and 1000 within the 10%
# that one seed of the internal transport varies by. Equal cylinders or blobs merged on their outer face must look as
# one of them.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
for case in 0 1 2 3 4 5 6 7 8 9 10 11 12 13; do
    for scale in 1 1000; do
        "$POVRAY" +i"$SRCDIR/tests/render/subsurface_coincident_seams.pov" +L"$SRCDIR/distribution/include" +w80 +h60 +wt2 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Case=$case Declare=S=$scale +o"seam$scale.ppm" >"seam$scale.log" 2>&1 ||
            { echo "case $case at scale $scale failed"; cat "seam$scale.log"; exit 1; }
    done
    export CASE=$case
    image_awk '
BEGIN {
    na = read_ppm("seam1.ppm", a); nb = read_ppm("seam1000.ppm", b)
    if (na != nb || na == 0) fail("seam images differ in size")
    first = sum_of(a, na); second = sum_of(b, nb)
    if (!(first > 0)) fail("case " ENVIRON["CASE"] " rendered black")
    if (abs(second / first - 1) > 0.10) fail("case " ENVIRON["CASE"] " changes with scene scale: " second / first)
}'
    cp seam1.ppm "case$case.ppm"
done
# equal shapes merged on a shared outer face look as one shape: no face goes dark where the cloud lost its points
for pair in "10 11" "12 13"; do
    set -- $pair
    export CASE=$1 REFERENCE=$2
    image_awk '
BEGIN {
    na = read_ppm("case" ENVIRON["CASE"] ".ppm", a); nb = read_ppm("case" ENVIRON["REFERENCE"] ".ppm", b)
    if (na != nb || na == 0) fail("seam images differ in size")
    first = sum_of(a, na); second = sum_of(b, nb)
    if (abs(first / second - 1) > 0.10) fail("case " ENVIRON["CASE"] " differs from one shape: " first / second)
}'
done
