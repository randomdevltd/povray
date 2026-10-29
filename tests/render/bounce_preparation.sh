#!/bin/sh
# bounce_preparation.sh <povray> <srcdir>: bounce preparation on one thread or several gives the same image.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=8; H=8
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_preparation.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" Declare=Sources=3 Declare=Samples=2 "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
for case in 0 1 2 3 4 5; do
    render one$case Declare=Case=$case +wt1
    render four$case Declare=Case=$case +wt4
    cmp -s one$case.pixels four$case.pixels || { echo "bounce_preparation: case $case differs on four threads" >&2; exit 1; }
done
grep -q 'Info: Preparation: bounce finished [0-9]*\.[0-9]s$' one0.log ||
    { cat one0.log; echo "bounce_preparation: no bounce progress line" >&2; exit 1; }
echo "bounce_preparation: bounce preparation is the same on any thread count"
