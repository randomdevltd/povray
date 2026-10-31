#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_visibility.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render clear_sphere
render opaque_sphere Declare=Opaque=1
render small_opaque_sphere Declare=Opaque=1 Declare=SceneScale=0.1
render clear_slab Declare=Kind=1
render blocked_slab Declare=Kind=1 Declare=Blocker=1
render scaled_blocked_slab Declare=Kind=1 Declare=Blocker=1 Declare=SceneScale=10
. "$SRCDIR/tests/render/image.sh"
image_awk '
BEGIN {
    split("clear_sphere clear_slab", lit, " ")
    for (i = 1; i <= 2; i++) {
        n = read_ppm(lit[i] ".ppm", values)
        if (!(sum_of(values, n) / n > 0.01)) fail(lit[i] ": mean " sum_of(values, n) / n ", maximum " max_of(values, n) "; expected mean above 0.01")
    }
    split("opaque_sphere small_opaque_sphere blocked_slab scaled_blocked_slab", dark, " ")
    for (i = 1; i <= 4; i++) {
        n = read_ppm(dark[i] ".ppm", values)
        if (max_of(values, n) != 0) fail(dark[i] ": maximum " max_of(values, n) ", mean " sum_of(values, n) / n "; expected black")
    }
    print "volume_visibility: opaque enclosing and external surfaces block the full sampled connection"
}'
