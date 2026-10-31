#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for case in 0 1 2 3 4; do
    "$POVRAY" +i"$SRCDIR/tests/render/interior_axis_media.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Declare=Case="$case" +o"axis$case.ppm" \
        > "axis$case.log" 2>&1 || { cat "axis$case.log"; exit 1; }
done
. "$SRCDIR/tests/render/image.sh"
image_awk '
BEGIN {
    split(".4 .6 .2", colour, " ")
    for (kind = 0; kind < 5; kind++) {
        n = read_ppm("axis" kind ".ppm", values)
        transmission = exp(-.2 * (kind == 2 || kind == 3 ? 2 : 3))
        emission = 0
        if (kind == 4) {
            q = exp(-.2)
            emission = .02 / .2 * (1 - q) * (1 + q / 1.1 ^ 2 + q * q)
        }
        line = ""
        for (c = 0; c < 3; c++) {
            target = colour[c + 1] * transmission + emission
            limit = max2(2 / 65535, .03 * target)
            for (i = c; i < n; i += 3)
                if (abs(values[i] - target) > limit)
                    fail(sprintf("interior axis %d channel %d: sample %d is %.7g, reference %.7g, limit %.7g", kind, c, i, values[i], target, limit))
            line = line sprintf(" %.7g", target)
        }
        print "interior axis " kind " reference" line
    }
}'
