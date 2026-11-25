#!/bin/sh
# cache_checkpoints.sh <povray> <srcdir>: a render killed part way through its bounce preparation leaves checkpoints of
# what it built; a restart reads them back, builds only the rest and renders the same image, at 1 and 16 threads and
# across thread counts, however many times it is killed; the finished cache replaces the checkpoints.
set -e
POVRAY=$1; SRCDIR=$2
W=48; H=36
DIR=cache_checkpoints_dir
SCENE="$SRCDIR/tests/render/prepared_caches.pov"
COMMON="+w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 Declare=Cells=20000 Declare=Nodes=1024"
render() {
    name=$1; shift
    "$POVRAY" +i"$SCENE" $COMMON +o"cache_checkpoints_$name.ppm" "$@" > "cache_checkpoints_$name.log" 2>&1 ||
        { cat "cache_checkpoints_$name.log"; echo "cache checkpoints: $name failed"; exit 1; }
    tail -c $((6 * W * H)) "cache_checkpoints_$name.ppm" > "cache_checkpoints_$name.px"
}
count() { tr '\n' ' ' < "cache_checkpoints_$1.log" | tr -s ' ' | grep -o "$2" | wc -l | tr -d ' '; }
pieces() { ls "$DIR"/*.povp 2> /dev/null | wc -l | tr -d ' '; }
# starts a render saving a checkpoint at every chance and kills it once `at` checkpoints exist
killed() {
    name=$1; at=$2; shift 2
    "$POVRAY" +i"$SCENE" $COMMON Cache_Path="$DIR" Cache_Checkpoint=0 +o"cache_checkpoints_$name.ppm" "$@" > "cache_checkpoints_$name.log" 2>&1 &
    pid=$!
    while [ "$(pieces)" -lt "$at" ] && kill -0 $pid 2> /dev/null; do sleep 0.01; done
    kill -9 $pid 2> /dev/null || { echo "cache checkpoints: $name finished before $at checkpoints"; exit 1; }
    wait $pid 2> /dev/null || true
    test ! -e "cache_checkpoints_$name.ppm" || test "$(ls "$DIR"/bounce0-*.povc 2> /dev/null | wc -l)" = 0 ||
        { echo "cache checkpoints: $name was killed after its bounce cache was finished"; exit 1; }
}

render ref1 +wt1
render ref16 +wt16
cmp -s cache_checkpoints_ref1.px cache_checkpoints_ref16.px || { echo "cache checkpoints: 1 and 16 threads render differently"; exit 1; }

# kills at several points, each restart from a fresh directory at the same or another thread count
for case in "1 16 16" "4 16 16" "12 1 1" "3 16 1" "8 1 16"; do
    set -- $case
    rm -rf "$DIR" && mkdir "$DIR"
    killed "kill$1" "$1" +wt$2
    left=$(pieces)
    render "resume$1" Cache_Path="$DIR" Cache_Checkpoint=0 +wt$3
    test "$(count "resume$1" 'bounce0: built and saved, [0-9.]* MiB, [0-9]* sections, [0-9.]* s; resumed from')" = 1 ||
        { cat "cache_checkpoints_resume$1.log"; echo "cache checkpoints: the restart after $1 checkpoints did not resume"; exit 1; }
    cmp -s cache_checkpoints_ref1.px "cache_checkpoints_resume$1.px" ||
        { echo "cache checkpoints: killed after $left checkpoints at $2 threads, resumed at $3, renders differently"; exit 1; }
    test "$(pieces)" = 0 || { echo "cache checkpoints: the finished cache left its checkpoints"; exit 1; }
    # the cache finished after a resume loads whole and renders the same
    render "load$1" Cache_Path="$DIR" +wt$3
    test "$(count "load$1" 'bounce0: loaded')" = 1 || { cat "cache_checkpoints_load$1.log"; echo "cache checkpoints: the resumed cache does not load"; exit 1; }
    cmp -s cache_checkpoints_ref1.px "cache_checkpoints_load$1.px" || { echo "cache checkpoints: the resumed cache loads differently"; exit 1; }
done

# killed again while resuming, and again: each restart goes on from the last checkpoint
rm -rf "$DIR" && mkdir "$DIR"
killed chain1 2 +wt16
killed chain2 6 +wt16
killed chain3 10 +wt1
render chain Cache_Path="$DIR" Cache_Checkpoint=0 +wt16
cmp -s cache_checkpoints_ref1.px cache_checkpoints_chain.px || { echo "cache checkpoints: a thrice killed render renders differently"; exit 1; }

# the finished cache verifies against a fresh build
render verify Cache_Path="$DIR" Cache_Verify=on +wt4
test "$(count verify 'bounce0: rebuilt and verified')" = 1 || { cat cache_checkpoints_verify.log; echo "cache checkpoints: a resumed cache differs from a fresh build"; exit 1; }

rm -rf "$DIR" cache_checkpoints_*.ppm cache_checkpoints_*.px cache_checkpoints_*.log
echo "cache checkpoints: killed part way at 1 and 16 threads, a render resumes from its checkpoints to the same image, and its finished cache replaces them"
