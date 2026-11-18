#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
for round in 0 1; do
  for split in 0 1; do
    for single in 0 32; do
        "$POVRAY" +i"$SRCDIR/tests/render/subsurface_single_seam.pov" +w16 +h12 +wt1 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Round="$round" Declare=Split="$split" Declare=Single="$single" \
            +o"slab-$round-$split-$single.ppm" >slab.log 2>&1 || { cat slab.log; exit 1; }
    done
  done
done
image_awk '
BEGIN {
    for (r = 0; r <= 1; r++) {
        for (s = 0; s <= 1; s++) {
            n = read_ppm("slab-" r "-" s "-0.ppm", diffuse)
            read_ppm("slab-" r "-" s "-32.ppm", combined)
            single[s] = 0
            for (i = 0; i < n; i += 3)
                single[s] += combined[i] - diffuse[i]
        }
        if (single[0] <= 0 || abs(single[1] / single[0] - 1) > 0.05)
            fail("single scattering changed across the inner seam (round=" r "): " single[0] " vs " single[1])
    }
}'
