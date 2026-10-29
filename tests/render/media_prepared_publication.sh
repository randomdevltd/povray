#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; case=$2; shift 2
    "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_publication.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" Declare=Draws=256 Declare=Case="$case" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    for field in 'Classic Draws' 'Classic Coeffs' 'Classic Calls' 'Modifier Fallbacks'; do
        if ! grep -Eq "$field:[[:space:]]+0([[:space:]]|$)" "$name.log"; then
            cat "$name.log"; exit 1
        fi
    done
}
. "$SRCDIR/tests/render/image.sh"
check() {
    image_awk -l joint_media -l volume_joint_media_reference \
        'BEGIN { volume_joint_media_check(ARGV[2], 0, ARGV[1] ".ppm", "") }' "$1" "$2"
}
render constant 0
check constant 0
render scalar 1
check scalar 1
render serial 2
check serial 0
render scalar_no_eager 1 -we
render serial_no_eager 2 -we
image_awk '
BEGIN {
    split("scalar serial", names, " ")
    for (i = 1; i <= 2; i++) {
        n = read_ppm(names[i] ".ppm", eager); w = IMAGE_W; h = IMAGE_H
        m = read_ppm(names[i] "_no_eager.ppm", lazy)
        if (w != IMAGE_W || h != IMAGE_H || !same_values(eager, n, lazy, m)) fail(names[i] ": the image changes without eager preparation")
    }
}'
if "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_publication.pov" +w8 +h8 +pr -a -d -p -v -gp \
    +wt2 -f Declare=Case=3 > context.log 2>&1; then
    cat context.log; exit 1
fi
grep -Fq 'Prepared density field requires ray context and cannot be published.' context.log
if grep -Eq 'Rendering\.\.\.|Rendered [0-9]+|Rendering line' context.log; then
    cat context.log; exit 1
fi
