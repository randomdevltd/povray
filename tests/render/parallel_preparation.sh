#!/bin/sh
# parallel_preparation.sh <povray> <srcdir>: lights prepared side by side and bounce splits spread over the workers give
# the same image and the same saved caches on 1, 4 and 16 threads, three runs each; Cache_Verify rebuilds each cache and
# compares, and cases without bounce also save their own caches, compared byte for byte, file names included.
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
fail() { cat "$1.log"; echo "parallel_preparation: $2" >&2; exit 1; }
for case in 0 1 2 3; do
    render one$case Declare=Case=$case +wt1 Cache_Path=caches$case
    built=$(count one$case ': built and saved')
    test "$built" -gt 0 || fail one$case "case $case saved no cache"
    for threads in 1 4 16; do
        for run in 1 2 3; do
            name=t$threads-$case-$run
            # the 1-thread build above is that thread count's first run
            if [ "$threads" = 1 ] && [ "$run" = 1 ]; then continue; fi
            # a bounce cache's note counts look-ahead work, which depends on the thread count, so it is verified, not compared
            if [ "$run" = 1 ] && [ "$case" != 0 ] && [ "$case" != 2 ]; then
                render $name Declare=Case=$case +wt$threads Cache_Path=caches$name
                diff -r caches$case caches$name > /dev/null || fail $name "case $case saves other cache files on $threads threads"
            else
                render $name Declare=Case=$case +wt$threads Cache_Path=caches$case Cache_Verify=on
                test "$(count $name 'verified identical')" = "$built" || fail $name "case $case caches differ on $threads threads"
            fi
            cmp -s one$case.pixels $name.pixels || fail $name "case $case differs on $threads threads, run $run"
        done
    done
done
# case 3 holds one light large enough to be prepared alone among the others, and its cache keeps its place in scene order
test "$(count one3 'Prepared cache light[0-6]: built and saved')" = 7 || fail one3 "case 3 did not save seven light caches"
case "$(ls -S caches3/light*.povc | head -n 1)" in caches3/light3-*) ;; *) fail one3 "case 3's large light is not cache light3";; esac
grep -q 'Info: Preparation cost: lights CPU [0-9.]*s, critical path [0-9.]*s' one1.log ||
    fail one1 "no preparation cost line"
