#!/bin/sh
# A buried light's glow does not depend on the second subsurface count: Single=0 lights the host exactly as a nonzero
# count does, at any thread count, and the internal path count follows the receiving finish's light_samples.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_buried_samples.pov" +w48 +h36 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 "$@" +o"$name.ppm" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render single0 +wt1 Declare=Single=0
render single32 +wt1 Declare=Single=32
render single0t3 +wt3 Declare=Single=0
render single0ls2 +wt1 Declare=Single=0 Declare=LightSamples=2
. "$SRCDIR/tests/render/image.sh"
image_awk '
BEGIN {
    n0 = read_ppm("single0.ppm", a)
    if (max_of(a, n0) <= 0) fail("Single=0 left the buried light dark")
    n32 = read_ppm("single32.ppm", b)
    if (!same_values(a, n0, b, n32)) fail("the buried glow changed with the second subsurface count")
    nt = read_ppm("single0t3.ppm", t)
    if (!same_values(a, n0, t, nt)) fail("the buried glow changed with the thread count")
    nl = read_ppm("single0ls2.ppm", l)
    if (same_values(a, n0, l, nl)) fail("light_samples 2 drew the same internal paths as light_samples 1")
    # The two counts estimate one mean glow, so their per-pixel difference is sampling noise about zero. Neighbouring
    # pixels share cloud points, so their noise is correlated: the error of the mean difference is taken from the spread
    # of the 48 block means of 6x6 pixels, which over 30 seeds matched the spread of the mean difference between seeds
    # (2.0e-5 each; single pixels gave 1.1e-5). A real shift lies beyond 4 such errors; the noise did at most 2.5.
    nb = 0; s = 0; ss = 0
    for (by = 0; by < IMAGE_H; by += 6)
        for (bx = 0; bx < IMAGE_W; bx += 6) {
            d = 0
            for (y = by; y < by + 6; y++)
                for (x = bx; x < bx + 6; x++)
                    for (c = 0; c < 3; c++) {
                        i = (y * IMAGE_W + x) * 3 + c
                        d += l[i] - a[i]
                    }
            d /= 108; nb++; s += d; ss += d * d
        }
    mean = s / nb
    error = sqrt((ss - nb * mean * mean) / (nb - 1) / nb)
    if (abs(mean) > 4 * error)
        fail("light_samples 2 moved the mean glow by " mean " from light_samples 1, more than 4 standard errors of " error)
}'
echo 'subsurface_buried_samples: buried glow independent of the single-scattering count, internal paths follow light_samples'
