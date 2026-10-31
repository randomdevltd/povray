#!/bin/sh
# csg_mesh_media_shadow.sh <povray> <srcdir>: shadow rays walk through translucent mesh CSG with media to the light.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=80; H=60
render() {
    "$POVRAY" +i"$SRCDIR/tests/render/csg_mesh_media_shadow.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"$1.ppm" +WT$2 \
        > "$1.log" 2>&1 || { cat "$1.log"; echo "csg_mesh_media_shadow: render with $2 thread(s) failed" >&2; exit 1; }
    tail -c $(( 6 * W * H )) "$1.ppm" > "$1.pixels"
}
render one 1
render four 4
cmp -s one.pixels four.pixels || { echo "csg_mesh_media_shadow: 1 and 4 threads differ" >&2; exit 1; }
echo "csg_mesh_media_shadow: shadow rays crossed the translucent mesh CSG"
