#!/bin/sh
# A body shown through its interior_texture looks its flesh up where light enters, as it does through its texture.
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
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_interior_flesh.pov" +w4 +h6 +wt1 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$arm.ppm" "$@" >"$arm.log" 2>&1 || { cat "$arm.log"; exit 1; }
}
render inward Declare=Inward=1
render outward Declare=Inward=0
image_awk '
BEGIN {
    n = read_ppm("outward.ppm", outward)
    read_ppm("inward.ppm", inward)
    if (n != 72) fail("expected 4x6 RGB pixels")
    if (!(max_of(outward, n) > 0)) fail("the block lit through its texture is dark")
    for (i = 0; i < n; ++i)
        if (inward[i] != outward[i]) fail("pixel " int(i / 3) " through the interior_texture is " inward[i] ", through the texture " outward[i])
}'
echo "subsurface_interior_flesh: pass"
