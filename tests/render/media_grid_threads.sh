#!/bin/sh
# media_grid_threads.sh <povray> <srcdir>: a prepared media grid built on one thread or several gives the same image.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=64; H=48
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_grid_threads.pov" +w$W +h$H -a -d -p -v -gp +fp16 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    ! grep -q 'using classic sampling' "$name.log" || { cat "$name.log"; echo "media_grid_threads: $name fell back to classic sampling" >&2; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
render one +wt1
render three +wt3
render four +wt4 +bs8
cmp -s one.pixels three.pixels || { echo "media_grid_threads: three threads differ from one" >&2; exit 1; }
cmp -s one.pixels four.pixels || { echo "media_grid_threads: four threads differ from one" >&2; exit 1; }
echo "media_grid_threads: the prepared grid is the same on any thread count"
