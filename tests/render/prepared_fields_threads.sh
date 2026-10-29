#!/bin/sh
# prepared_fields_threads.sh <povray> <srcdir>: refraction and interior light fields testing an object prepare alike on any thread count.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=48; H=36
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/prepared_fields_threads.pov" +w$W +h$H -a -d -p -v -gp +fp16 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
for field in 1 2; do
    render one$field +wt1 Declare=Field=$field
    render six$field +wt6 Declare=Field=$field
    cmp -s one$field.pixels six$field.pixels || { echo "prepared_fields_threads: Field=$field differs on six threads" >&2; exit 1; }
done
echo "prepared_fields_threads: refraction and interior light fields are the same on one thread and six"
