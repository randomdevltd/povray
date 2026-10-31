#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for clear in 0 1 2; do
    "$POVRAY" +i"$SRCDIR/tests/render/refraction_clear_axis.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Clear="$clear" +o"clear$clear.ppm" \
        > "clear$clear.log" 2>&1 || { cat "clear$clear.log"; exit 1; }
done
. "$SRCDIR/tests/render/image.sh"
image_awk -l refraction_prepared_ray_reference '
BEGIN {
    for (clear = 0; clear < 3; clear++) {
        read_ppm("clear" clear ".ppm", pixels)
        error = screen_error(pixels, IMAGE_W, IMAGE_H, 1)
        print "clear " clear " maximum coordinate error " error
        if (!(error <= .001)) fail("clear " clear ": maximum coordinate error " error " exceeds .001")
    }
}'
