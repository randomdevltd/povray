#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_joint_media.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" Declare=Draws=256 "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
. "$SRCDIR/tests/render/image.sh"
check() {
    image_awk -l joint_media -l volume_joint_media_reference \
        'BEGIN { volume_joint_media_check(ARGV[2], ARGV[3], ARGV[1] ".ppm", ARGV[1] "-reference.txt") }' "$@"
}
render homogeneous Declare=Case=0
check homogeneous 0 0
render heterogeneous Declare=Case=1
check heterogeneous 1 0
render multiply Declare=Case=2
check multiply 2 0
render plate Declare=Case=3
check plate 3 0
render phase Declare=Case=0 Declare=Phase=0.4
check phase 0 0.4
render subdivision Declare=Case=0 Declare=Resolution=0.025
check subdivision 0 0
image_awk '
BEGIN {
    for (c = 0; c < 3; c++) {
        getline first < "homogeneous-reference.txt"; split(first, a, " ")
        getline second < "subdivision-reference.txt"; split(second, b, " ")
        limit = max2(2 / 65535, 0.03 * a[3])
        if (!(abs(a[2] - b[2]) <= limit))
            fail("channel " c ": homogeneous mean " a[2] ", subdivided mean " b[2] ", limit " limit)
    }
}'
