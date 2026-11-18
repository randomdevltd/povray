#!/bin/sh
# Method 2 on the surface map weighs a far group of cloud points as one point: its light must be the group's light per
# area, not its light times its area. Before, a long channel read the summed light and the face came out red and speckled.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
for cells in 1024 16384; do
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_far_groups.pov" +w32 +h32 +wt2 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=C=$cells +o"slab$cells.ppm" >slab$cells.log 2>&1 ||
        { echo "domain_cells $cells failed"; cat slab$cells.log; exit 1; }
done
image_awk '
BEGIN {
    for (k = 0; k < 2; k++) {
        n = read_ppm(k ? "slab16384.ppm" : "slab1024.ppm", v)
        r = 0; g = 0; rr = 0
        for (i = 0; i < n; i += 3) { r += v[i]; rr += v[i] * v[i]; g += v[i + 1] }
        m = n / 3; red[k] = r / m; green[k] = g / m; spread[k] = sqrt(max2(0, rr / m - red[k] * red[k])) / red[k]
        if (!(red[k] > 0 && green[k] > 0)) fail("the slab rendered black")
        if (spread[k] > 0.01) fail("the evenly lit face is speckled: red varies by " spread[k] " of its mean")
    }
    if (abs(red[1] / red[0] - 1) > 0.03) fail("the face is " red[1] / red[0] " as red at 16384 cells as at 1024")
}'
