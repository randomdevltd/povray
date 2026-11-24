#!/bin/sh
# parallel_preparation.sh <povray> <srcdir>: lights prepared side by side and bounce splits spread over the workers give
# the same image and the same saved caches on 1, 4 and 16 threads; Cache_Verify rebuilds each cache and compares.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=32; H=18
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/parallel_preparation.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" Declare=Count=4 Declare=Cells=512 Declare=LightCells=4096 "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
# the console wraps long lines, so a log is joined back into one line before counting
count() { tr '\n' ' ' < "$1.log" | tr -s ' ' | grep -o "$2" | wc -l | tr -d ' '; }
for case in 0 1 2; do
    render one$case Declare=Case=$case +wt1 Cache_Path=caches$case
    built=$(count one$case ': built and saved')
    test "$built" -gt 0 || { cat one$case.log; echo "parallel_preparation: case $case saved no cache" >&2; exit 1; }
    for threads in 4 16; do
        render t$threads-$case Declare=Case=$case +wt$threads Cache_Path=caches$case Cache_Verify=on
        cmp -s one$case.pixels t$threads-$case.pixels ||
            { echo "parallel_preparation: case $case differs on $threads threads" >&2; exit 1; }
        test "$(count t$threads-$case 'verified identical')" = "$built" ||
            { cat t$threads-$case.log; echo "parallel_preparation: case $case caches differ on $threads threads" >&2; exit 1; }
    done
done
grep -q 'Info: Preparation cost: lights CPU [0-9.]*s, critical path [0-9.]*s' one1.log ||
    { cat one1.log; echo "parallel_preparation: no preparation cost line" >&2; exit 1; }
