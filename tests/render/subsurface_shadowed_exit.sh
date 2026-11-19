#!/bin/sh
# A shadowed exit point on a coarse surface-map cloud takes the light entering around it, not its own darkness alone.
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
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_shadowed_exit.pov" +w8 +h100 +wt"${THREADS:-1}" +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$arm.ppm" "$@" >"$arm.log" 2>&1 || { cat "$arm.log"; exit 1; }
}
render slit
render open Declare=Upper=0
THREADS=4 render parallel
grep -q "near field drawn at [1-9]" slit.log || { cat slit.log; exit 1; }
image_awk '
BEGIN {
    n = read_ppm("slit.ppm", slit)
    read_ppm("open.ppm", open)
    read_ppm("parallel.ppm", parallel)
    if (n != 2400) fail("expected 8x100 RGB pixels")
    for (i = 0; i < n; ++i)
        if (parallel[i] != slit[i]) fail("worker count changed the output")
    # row 50 of column 4 lies in the slit; the open face is lit directly, the slit floor only through the material
    i = (50 * 8 + 4) * 3 + 1
    if (!(open[i] > 0)) fail("the open face is dark")
    if (!(16 * slit[i] > open[i])) fail("the shadowed slit floor is dark: " slit[i] " against " open[i] " open")
    if (!(slit[i] < open[i])) fail("the shadowed slit floor is brighter than the open face")
}'
