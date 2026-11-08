#!/bin/sh
# thread_identity.sh <povray> <srcdir>: shadows a thread's shadow cache may hold render byte for byte the same
# on one thread as on two, four and eight, in any block size and order.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=128; H=96
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/thread_identity.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; echo "thread_identity: $name failed" >&2; exit 1; }
    tail -c $((6 * W * H)) "$name.ppm" > "$name.px"
}
same() {
    cmp -s "$1.px" "$2.px" || { echo "thread_identity: $1 and $2 differ" >&2; exit 1; }
}
render one +wt1
render two +wt2 +bs16
render four +wt4 +bs8
render eight +wt8 +bs5 +rp5
same one two
same one four
same one eight
echo "thread_identity: shadows are the same on 1, 2, 4 and 8 threads and in any block order"
