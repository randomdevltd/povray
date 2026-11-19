#!/bin/sh
# A volume light filling a subsurface body whose members each hold their own copy of its material lights the body: the
# search for where its light leaves the material runs through the whole body, as its cloud points do.
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
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_body_volume_light.pov" +w8 +h4 +wt"${THREADS:-1}" +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$arm.ppm" "$@" >"$arm.log" 2>&1 || { cat "$arm.log"; exit 1; }
}
render one
THREADS=4 render parallel
image_awk '
BEGIN {
    n = read_ppm("one.ppm", one)
    read_ppm("parallel.ppm", parallel)
    if (n != 96) fail("expected 8x4 RGB pixels")
    for (i = 0; i < n; ++i) {
        if (parallel[i] != one[i]) fail("worker count changed the output")
        if (!(one[i] > 0)) fail("a pixel of the lit body is dark")
    }
}'
