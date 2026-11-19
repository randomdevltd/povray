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
    ratio = sum_of(l, nl) / sum_of(a, n0)
    if (abs(ratio - 1) > 0.05) fail("light_samples 2 moved the mean glow by " ratio " of light_samples 1, tolerance 5%")
}'
echo 'subsurface_buried_samples: buried glow independent of the single-scattering count, internal paths follow light_samples'
