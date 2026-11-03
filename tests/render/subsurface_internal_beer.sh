#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for depth in 0.35 0.60; do
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_beer.pov" +w1 +h1 +wt1 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=SourceDepth=$depth +o"beer$depth.ppm" >"beer$depth.log" 2>&1 ||
        { cat "beer$depth.log"; exit 1; }
done
. "$SRCDIR/tests/render/image.sh"
image_awk '
BEGIN {
    for (i = 0; i < 2; ++i) {
        depth = i == 0 ? 0.35 : 0.60
        name = i == 0 ? "beer0.35.ppm" : "beer0.60.ppm"
        n = read_ppm(name, v)
        actual = sum_of(v, n) / n
        expected = 0.5 * (exp(-(depth - 0.12)) - exp(-(depth + 0.12)))
        if (abs(actual / expected - 1) > 0.005)
            fail("internal source Beer radiance " actual ", expected " expected ", tolerance 0.5%")
    }
}'
echo 'subsurface_internal_beer: finite-source chord radiance and burial attenuation passed'
