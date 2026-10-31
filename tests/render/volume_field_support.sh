#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for value in 1 2 3 4 5 6; do
    "$POVRAY" +i"$SRCDIR/tests/render/volume_field_support.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Declare=Case="$value" +o"$value.ppm" > "$value.log" 2>&1 || { cat "$value.log"; exit 1; }
done
. "$SRCDIR/tests/render/image.sh"
image_awk '
BEGIN {
    split("1 2 empty container padding|3 4 open and closed occupancy|5 6 explicit and clipped box", pairs, "|")
    for (p = 1; p <= 3; p++) {
        split(pairs[p], field, " ")
        ne = read_ppm(field[1] ".ppm", expected)
        na = read_ppm(field[2] ".ppm", actual)
        compare(substr(pairs[p], 5), actual, na, expected, ne, 0.10)
    }
}'
