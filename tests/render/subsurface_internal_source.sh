#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for setting in 1 2; do
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_source.pov" +w4 +h4 +wt2 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Power=0.0$setting +o"internal$setting.ppm" >"internal$setting.log" 2>&1 ||
        { cat "internal$setting.log"; exit 1; }
done
. "$SRCDIR/tests/render/image.sh"
image_awk '
BEGIN {
    na = read_ppm("internal1.ppm", a); nb = read_ppm("internal2.ppm", b)
    if (na != nb || na == 0) fail("internal source image sizes differ")
    first = sum_of(a, na); second = sum_of(b, nb)
    if (!(first > 0.0001 && second > first)) fail("buried source failed to light the material")
    if (abs(second / first - 2) > 0.03) fail("buried source power is not linear: " second / first)
}'
for passes in 1 2; do
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_internal_source.pov" +w24 +h24 +wt2 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Power=0.1 Declare=BouncePasses="$passes" \
        +o"order$passes.ppm" >"order$passes.log" 2>&1 ||
        { cat "order$passes.log"; exit 1; }
done
image_awk '
BEGIN {
    na = read_ppm("order1.ppm", a); nb = read_ppm("order2.ppm", b)
    if (na != nb || na == 0) fail("internal source bounce image sizes differ")
    first = sum_of(a, na); second = sum_of(b, nb)
    if (!(first > 0.0001)) fail("buried source failed to illuminate the external receiver")
    if (abs(second / first - 1) > 0.02) fail("primary source was reinjected after its first order: " second / first)
}'
echo 'subsurface_internal_source: buried volume emission, linear power and source-order isolation passed'
