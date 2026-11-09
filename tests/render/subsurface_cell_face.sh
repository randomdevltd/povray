#!/bin/sh
# A method 2 cap on a face of the point cloud's cells, lit and seen down its axis, must be lit alike on every side of
# the axis: the cloud must hold its points in one cell whichever way their coordinates round. Before, the points on
# one side of a plane through the axis fell between the cells and that half was lit as if alone.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
. "$SRCDIR/tests/render/image.sh"
for turn in 0 37 90; do
    for scale in 1 1000; do
        "$POVRAY" +i"$SRCDIR/tests/render/subsurface_cell_face.pov" +w48 +h48 +wt2 +pr -a -d -p -v -gp \
            +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=R=$turn Declare=S=$scale +o"face.ppm" >face.log 2>&1 ||
            { echo "turn $turn at scale $scale failed"; cat face.log; exit 1; }
        tr '\n' ' ' <face.log | tr -s ' ' | grep -q "with none facing in the disc 0;" ||
            { echo "turn $turn at scale $scale: cap points found no cloud points around them"; grep -A2 "none facing" face.log; exit 1; }
        export CASE="turn $turn scale $scale"
        image_awk '
BEGIN {
    n = read_ppm("face.ppm", v); w = IMAGE_W; h = IMAGE_H
    # the four half-discs about the axis: above, below, left and right of the image centre
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        g = v[3 * (y * w + x) + 1]
        if (y < h / 2) top += g; else bottom += g
        if (x < w / 2) left += g; else right += g
    }
    if (!(top > 0 && bottom > 0 && left > 0 && right > 0)) fail(ENVIRON["CASE"] ": the cap rendered black")
    worst = max2(abs(top / bottom - 1), abs(left / right - 1))
    if (worst > 0.04) fail(ENVIRON["CASE"] ": the cap is lit unevenly about its axis by " worst " (top " top ", bottom " bottom ", left " left ", right " right ")")
}'
    done
done
