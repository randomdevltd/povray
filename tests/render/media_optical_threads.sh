#!/bin/sh
# media_optical_threads.sh <povray> <srcdir>: optical-depth grids built on one thread or several give the same image.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=64; H=48
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_optical_threads.pov" +w$W +h$H -a -d -p -v -gp +fp16 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
render one +wt1
render three +wt3
render four +wt4 +bs8
cmp -s one.pixels three.pixels || { echo "media_optical_threads: three threads differ from one" >&2; exit 1; }
cmp -s one.pixels four.pixels || { echo "media_optical_threads: four threads differ from one" >&2; exit 1; }
echo "media_optical_threads: the optical-depth grids are the same on any thread count"
