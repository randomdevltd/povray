#!/bin/sh
# distribution/scenes/subsurface/buried-veins.pov: its one pigment-shaped buried light lights the hand through the hand's
# point cloud. A small, coarse render keeps it quick.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
"$POVRAY" +i"$SRCDIR/distribution/scenes/subsurface/buried-veins.pov" +L"$SRCDIR/distribution/scenes/subsurface" \
    +L"$SRCDIR/distribution/include" +w24 +h16 -a -d -p -v -gp +wt4 +fp16 File_Gamma=1 Stochastic_Seed=0 \
    Declare=View=1 Declare=Cells=4000 Declare=Samples=1 Declare=Fill=0 Declare=LightCells=100000 +oveins.ppm >veins.log 2>&1 ||
    { cat veins.log; exit 1; }
grep -E 'Prepared interior light grid' veins.log >/dev/null ||
    { echo "subsurface_buried_veins: the vein light was not prepared" >&2; cat veins.log; exit 1; }
grep -E 'Method 2 buried lights: [1-9][0-9]* shading points' veins.log >/dev/null ||
    { echo "subsurface_buried_veins: the cloud did not serve the buried light" >&2; cat veins.log; exit 1; }
echo 'subsurface_buried_veins: the vein light is prepared and the cloud serves its light'
