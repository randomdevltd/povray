#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1
    "$POVRAY" +i"$SRCDIR/tests/render/$name.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render media_mixed_prepared
for field in 'Prepared4 Queries' 'Legacy3 Queries'; do
    grep -Eq "$field:[[:space:]]+[1-9][0-9]*" media_mixed_prepared.log || { cat media_mixed_prepared.log; exit 1; }
done
for field in 'Forbidden4 Procedure' 'Old Integrator'; do
    grep -Eq "$field:[[:space:]]+0([[:space:]]|$)" media_mixed_prepared.log || { cat media_mixed_prepared.log; exit 1; }
done
. "$SRCDIR/tests/render/image.sh"
image_awk -l joint_media -l media_mixed_prepared_reference 'BEGIN { media_mixed_prepared_check(ARGV[1]) }' media_mixed_prepared.ppm
render media_prepared_infinite
image_awk '
BEGIN {
    n = read_ppm("media_prepared_infinite.ppm", pixels)
    split(".2 .4 .6", sigma, " ")
    for (c = 0; c < 3; c++) {
        target = exp(-2 * sigma[c + 1])
        limit = max2(2 / 65535, .03 * target)
        for (i = c; i < n; i += 3)
            if (abs(pixels[i] - target) > limit)
                fail(sprintf("media_prepared_infinite channel %d: sample %d is %.7g, Beer reference %.7g, limit %.7g", c, i, pixels[i], target, limit))
    }
}'
grep -Eq 'Prepared4 Queries:[[:space:]]+[1-9][0-9]*' media_prepared_infinite.log
for field in 'Forbidden4 Procedure' 'Old Integrator'; do
    grep -Eq "$field:[[:space:]]+0([[:space:]]|$)" media_prepared_infinite.log || { cat media_prepared_infinite.log; exit 1; }
done
