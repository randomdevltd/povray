#!/bin/sh
# prepared_caches.sh <povray> <srcdir>: prepared caches are saved, loaded pixel for pixel, verified against a rebuild,
# rejected and rebuilt when damaged, and rebuilt when the scene changes: an input edited during the parse saves nothing,
# swapped file contents and a changed image width that the scene reads are new keys, and other keys' files are kept.
set -e
POVRAY=$1; SRCDIR=$2
W=48; H=36
DIR=prepared_caches_dir
rm -rf "$DIR" && mkdir "$DIR"
run() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/prepared_caches.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 +wt4 \
        +o"prepared_caches_$name.ppm" "$@" > "prepared_caches_$name.log" 2>&1 || { cat "prepared_caches_$name.log"; exit 1; }
    tail -c $((6 * W * H)) "prepared_caches_$name.ppm" > "prepared_caches_$name.px"
}
# the console wraps long lines, so a log is joined back into one line before counting
count() { tr '\n' ' ' < "prepared_caches_$1.log" | tr -s ' ' | grep -o "$2" | wc -l | tr -d ' '; }
same() { cmp -s prepared_caches_first.px "prepared_caches_$1.px" || { echo "prepared caches: $1 renders differently from the first build"; exit 1; }; }
# flips a byte of a file in place
damage() {
    cp "$1" prepared_caches_copy
    printf 'Z' | dd of="$1" bs=1 seek="$2" conv=notrunc 2> /dev/null
    cmp -s "$1" prepared_caches_copy && printf 'z' | dd of="$1" bs=1 seek="$2" conv=notrunc 2> /dev/null
    rm -f prepared_caches_copy
}

run plain
test "$(count plain 'Prepared cache')" = 0 || { echo "prepared caches: saved without Save_Caches or Cache_Path"; exit 1; }
run first Cache_Path="$DIR"
for kind in media0 light0 bounce0; do
    test "$(count first "Prepared cache $kind: built and saved")" = 1 || { cat prepared_caches_first.log; echo "prepared caches: $kind was not saved"; exit 1; }
done
built=$(count first ': built and saved')
cmp -s prepared_caches_plain.px prepared_caches_first.px || { echo "prepared caches: saving changed the image"; exit 1; }

run second Cache_Path="$DIR"
test "$(count second ': loaded')" = "$built" && test "$(count second ': built')" = 0 ||
    { echo "prepared caches: $(count second ': loaded') of $built caches loaded"; exit 1; }
same second

run verify Cache_Path="$DIR" Cache_Verify=on +wt2
test "$(count verify 'verified identical')" = "$built" || { cat prepared_caches_verify.log; echo "prepared caches: a rebuild differs from its saved cache"; exit 1; }
same verify

# a damaged section and a truncated file are rejected, rebuilt and render the same
bounce=$(ls "$DIR"/bounce0-*.povc)
damage "$bounce" $(($(wc -c < "$bounce") / 2))
light=$(ls "$DIR"/light0-*.povc)
head -c $(($(wc -c < "$light") - 10)) "$light" > prepared_caches_cut && mv prepared_caches_cut "$light"
run damaged Cache_Path="$DIR"
test "$(count damaged 'is rejected')" = 2 && test "$(count damaged 'rebuilt after rejecting a damaged file')" = 2 ||
    { cat prepared_caches_damaged.log; echo "prepared caches: damaged files were not rejected"; exit 1; }
same damaged
run again Cache_Path="$DIR"
test "$(count again ': loaded')" = "$built" || { echo "prepared caches: rebuilt caches were not saved again"; exit 1; }
same again

# another lamp colour is another key: every cache rebuilds, and the image changes
run changed Cache_Path="$DIR" Declare=Tint=0.9
test "$(count changed 'its key changed')" = "$built" || { cat prepared_caches_changed.log; echo "prepared caches: a changed scene reused a cache"; exit 1; }
! cmp -s prepared_caches_first.px prepared_caches_changed.px || { echo "prepared caches: a changed scene rendered the same"; exit 1; }

# the first key's files were kept for any other job sharing the directory, and still load
run back Cache_Path="$DIR"
test "$(count back ': loaded')" = "$built" || { cat prepared_caches_back.log; echo "prepared caches: another key's build evicted these caches"; exit 1; }
same back
# eager threads built the changed key's media grids, since only the first key's media caches were saved
test "$(count second 'Eager media grids:')" = 0 || { cat prepared_caches_second.log; echo "prepared caches: media grids were built ahead though they load"; exit 1; }
test "$(count changed 'Eager media grids: 2 of 2 prepared ahead')" = 1 || { cat prepared_caches_changed.log; echo "prepared caches: another key's files stopped eager media builds"; exit 1; }

# each file counts by its name in the scene: swapping two files' contents is another key, though the same contents are read
printf '#declare Tint = 0.6;\n' > prepared_caches_a.inc; printf '#declare Tint = 0.9;\n' > prepared_caches_b.inc
run named Cache_Path="$DIR" Declare=Inputs=1
printf '#declare Tint = 0.9;\n' > prepared_caches_a.inc; printf '#declare Tint = 0.6;\n' > prepared_caches_b.inc
run swapped Cache_Path="$DIR" Declare=Inputs=1
test "$(count swapped ': loaded')" = 0 || { cat prepared_caches_swapped.log; echo "prepared caches: swapped input files reused a cache"; exit 1; }
same swapped

# a file edited after the parse read it saves nothing, so its new contents cannot find caches built from the old
# the second file asks for the rewrite, so both renders read the same files and declares
printf '#declare Tint = 0.6;\n' > prepared_caches_a.inc; printf '#declare Rewrite = 1;\n' > prepared_caches_b.inc
run edited Cache_Path="$DIR" Declare=Inputs=1
test "$(count edited 'changed during the parse')" = 1 && test "$(count edited ': built and saved')" = 0 ||
    { cat prepared_caches_edited.log; echo "prepared caches: an input edited during the parse was saved"; exit 1; }
same edited
run tint9 Declare=Tint=0.9
run afteredit Cache_Path="$DIR" Declare=Inputs=1
test "$(count afteredit ': loaded')" = 0 || { cat prepared_caches_afteredit.log; echo "prepared caches: an edited input loaded caches of its old contents"; exit 1; }
cmp -s prepared_caches_tint9.px prepared_caches_afteredit.px || { echo "prepared caches: the edited input rendered wrongly"; exit 1; }

# a value read through global.image_width keys the caches it shapes; a scene that reads no size shares them across sizes
run wide48 Cache_Path="$DIR" Declare=ByWidth=1
W=64; H=48
run wide64 Cache_Path="$DIR" Declare=ByWidth=1
test "$(count wide64 ': loaded')" = 0 || { cat prepared_caches_wide64.log; echo "prepared caches: a changed image width read by the scene reused a cache"; exit 1; }
run wide64plain Declare=ByWidth=1
cmp -s prepared_caches_wide64plain.px prepared_caches_wide64.px || { echo "prepared caches: the wider image rendered wrongly"; exit 1; }
run size64 Cache_Path="$DIR"
test "$(count size64 ': loaded')" = "$built" || { cat prepared_caches_size64.log; echo "prepared caches: a scene that reads no size rebuilt at another size"; exit 1; }
W=48; H=36

rm -rf "$DIR" prepared_caches_*.ppm prepared_caches_*.px prepared_caches_*.log prepared_caches_?.inc
echo "prepared caches: saved, loaded pixel for pixel, verified, rejected when damaged, rebuilt when the scene, an input's contents or name, or a size it reads changed, never saved from an input edited mid-parse, other keys kept"
