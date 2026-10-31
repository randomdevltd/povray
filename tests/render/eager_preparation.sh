#!/bin/sh
# eager_preparation.sh <povray> <srcdir>: meshes built ahead on spare threads render and report exactly as with Eager_Preparation off.
set -e
POVRAY=$1; SRCDIR=$2
run() {
    run_name=$1; run_scene=$2; shift 2
    "$POVRAY" +i"$SRCDIR/tests/render/$run_scene.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +L. +w96 +h32 -a -d -p -v -gp +fp \
        +o"eager_$run_name.ppm" "$@" > "eager_$run_name.log" 2>&1 || { cat "eager_$run_name.log"; exit 1; }
    tail -c $((96 * 32 * 3)) "eager_$run_name.ppm" > "eager_$run_name.px"
}
same() { cmp -s "eager_$1.px" "eager_$2.px" || { echo "eager preparation: $1 and $2 differ"; exit 1; }; }
flat() { tr '\n' ' ' < "eager_$1.log" | tr -s ' '; }
line() { flat "$1" | grep -o "Deferred meshes: [^.]*\." || true; }
eager() { flat "$1" | grep -o "Eager preparation: [^;]*; [^;]*;" || true; }
# on at +wt4 and +wt8 against off, same image and the same mesh counts; +wt1 has no eager threads
compare() {
    name=$1; scene=$2; shift 2
    run "${name}_off" "$scene" +wt4 -WE "$@"
    ! flat "${name}_off" | grep -q "Eager preparation:" || { echo "eager preparation: -WE left it on"; exit 1; }
    run "${name}_on1" "$scene" +wt1 "$@"
    same "${name}_on1" "${name}_off"
    ! flat "${name}_on1" | grep -q "Eager preparation:" || { echo "eager preparation: +wt1 built ahead"; exit 1; }
    for wt in 4 8; do
        run "${name}_on$wt" "$scene" +wt$wt "$@"
        same "${name}_on$wt" "${name}_off"
        test "$(line "${name}_on$wt")" = "$(line "${name}_off")" || { echo "eager preparation: $name at +wt$wt: $(line "${name}_on$wt") against $(line "${name}_off")"; exit 1; }
        test -z "$(line "${name}_on$wt")" || test -n "$(eager "${name}_on$wt")" || { echo "eager preparation: $name has no summary at +wt$wt"; exit 1; }
    done
}
# a setting read by a mesh function changes after the mesh is recorded: the eager build is redone
for mode in 0 1 2; do
    compare "late$mode" eager_preparation Declare=Mode=$mode Declare=Tail=500000
    eager "late${mode}_on8" | grep -q " [1-9][0-9]* redone" || { echo "eager preparation: mode $mode redid nothing: $(eager "late${mode}_on8")"; exit 1; }
done
# a pigment function running functions (directly, in a pigment map, two levels deep, shared by two slots) builds ahead on a
# snapshot of them all; one whose pigment tests an object waits for the end of the parse, and the log says so
left() { flat "$1" | grep -o "[0-9]* left to the end of the parse" | cut -d' ' -f1; }
for mode in 6 8 9 10; do
    compare "nested$mode" eager_preparation Declare=Mode=$mode Declare=Tail=500000
    eager "nested${mode}_on4" | grep -q "and 2 of 2 scene meshes built ahead" || { echo "eager preparation: mode $mode: $(eager "nested${mode}_on4")"; exit 1; }
    test "$(left "nested${mode}_on4")" = 0 || { echo "eager preparation: mode $mode left meshes to the parse end"; exit 1; }
done
compare probe eager_preparation Declare=Mode=7 Declare=Tail=500000
test "$(left probe_on4)" = 5 || { echo "eager preparation: an object test left $(left probe_on4) of 5 meshes to the parse end"; exit 1; }
# a media density grid recorded with its container is prepared before the render, at least partly during the parse, unless a
# setting it read changed, no view keeps it or its density tests an object
grids() { flat "$1" | grep -o "Eager media grids: [0-9]* of [0-9]* prepared" || true; }
field() { flat "$1" | grep -o "$2" | grep -o "[0-9.]*" | head -1; }
during() { field "$1" "[0-9]* of their [0-9]* slabs built during the parse"; }
expect() {
    test "$(grids "$1")" = "Eager media grids: $2 prepared" || { echo "eager preparation: $1: $(grids "$1"), expected $2"; exit 1; }
    test "$(field "$1" "[0-9]* more require final preparation")" = "$3" || { echo "eager preparation: $1: $(field "$1" "[0-9]* more require final preparation") left as no snapshot can take them, expected $3"; exit 1; }
}
compare media eager_media Declare=Tail=1500000
expect media_on8 "1 of 1" 0
test "$(during media_on8)" -gt 0 || { echo "eager preparation: no slab was built during the parse"; exit 1; }
for late in 1 2 4; do
    compare "medialate$late" eager_media Declare=Late=$late Declare=Tail=1500000
    expect "medialate${late}_on8" "0 of 1" 0
done
compare mediatag eager_media Declare=Tagged=1 Declare=Tail=1500000
expect mediatag_on8 "0 of 1" 0
compare mediakind1 eager_media Declare=Kind=1 Declare=Tail=1500000
expect mediakind1_on8 "1 of 1" 0
compare mediakind2 eager_media Declare=Kind=2 Declare=Tail=1500000
expect mediakind2_on8 "0 of 0" 1
compare mediashared eager_media Declare=Kind=3 Declare=Tail=1500000
expect mediashared_on8 "2 of 2" 0
compare mediamerge eager_media Declare=Kind=4 Declare=Tail=1500000
expect mediamerge_on8 "1 of 1" 0
status=0; timeout -k 10 120 "$POVRAY" +i"$SRCDIR/tests/render/eager_media.pov" +L"$SRCDIR/include" +w8 +h8 -d -p -gp -f +wt4 \
    Declare=Fail=1 Declare=Tail=200000 > eager_mediafail.log 2>&1 || status=$?
test "$status" = 1 || { cat eager_mediafail.log; echo "eager preparation: the media error scene exited $status"; exit 1; }
grep -q "stop with the grid half built" eager_mediafail.log || { cat eager_mediafail.log; echo "eager preparation: no media parse error"; exit 1; }
run early eager_preparation +wt4 -WE Declare=Late=0
! cmp -s eager_early.px eager_late0_off.px || { echo "eager preparation: the noise generator changed nothing"; exit 1; }
# a long parse lets the eager threads finish the meshes the filter rejects before they are discarded
compare tags eager_preparation Declare=Mode=3 Declare=Tail=4000000
eager tags_on4 | grep -q " [1-9][0-9]* build(s) discarded" || { echo "eager preparation: nothing discarded: $(eager tags_on4)"; exit 1; }
compare meshes render_tags_meshes 'Filter_Tags=!"b"'
# a filter given at the start gates builds on placement: the never-placed mesh is not built, none is discarded, and no warning
compare gated eager_preparation Declare=Tail=500000 'Filter_Tags=!"none"'
eager gated_on4 | grep -q " 0 build(s) discarded" || { echo "eager preparation: a gated build was discarded: $(eager gated_on4)"; exit 1; }
eager gated_on4 | grep -q " [1-9][0-9]* of [0-9]* scene meshes built ahead" || { echo "eager preparation: the gate queued nothing: $(eager gated_on4)"; exit 1; }
! flat gated_on4 | grep -q "Warning: Eager preparation built" || { echo "eager preparation: warned with nothing discarded"; exit 1; }
# the warning follows the discarded CPU time the summary reports: from one second up, and never below
run waste eager_preparation +wt4 Declare=Mode=3 Declare=Fine=1 Declare=Queries=0 Declare=Tail=3000000
wasted=$(flat waste | grep -o "discarded unused ([0-9.]* s CPU)" | grep -o "[0-9.]*" | head -1)
if awk -v t="$wasted" 'BEGIN { exit !(t >= 1) }'; then
    flat waste | grep -q "Warning: Eager preparation built [0-9]* mesh(es) that the scene's tag filters left out" || { echo "eager preparation: no warning for $wasted s discarded"; exit 1; }
else
    ! flat waste | grep -q "Warning: Eager preparation built" || { echo "eager preparation: warned for $wasted s discarded"; exit 1; }
fi
! flat meshes_on4 | grep -q "Warning: Eager preparation built" || { echo "eager preparation: warned about a trivial discard"; exit 1; }
# with no filter, slow builds of a mesh declared but never placed get the other advice
run unplaced eager_preparation +wt4 Declare=Fine=1 Declare=Queries=0 Declare=Tail=3000000
unplaced=$(flat unplaced | grep -o "discarded unused ([0-9.]* s CPU)" | grep -o "[0-9.]*" | head -1)
! awk -v t="$unplaced" 'BEGIN { exit !(t >= 1) }' || flat unplaced | grep -q "Warning: Eager preparation built [0-9]* mesh(es) declared but never placed" || { echo "eager preparation: no unplaced warning for $unplaced s"; exit 1; }
# isosurface_mesh, skein_mesh and a .povm octahedron
compare cache mesh_cache_check
le32() { printf "\\$(printf %03o $(($1 & 255)))\\$(printf %03o $(($1 >> 8 & 255)))\\$(printf %03o $(($1 >> 16 & 255)))\\$(printf %03o $(($1 >> 24 & 255)))"; }
{
    printf POVM; for n in 1 0 6 0 0 8; do le32 $n; done
    for v in 1065353216 0 0 3212836864 0 0 0 1065353216 0 0 3212836864 0 0 0 1065353216 0 0 3212836864; do le32 $v; done
    for i in 0 2 4 2 1 4 1 3 4 3 0 4 2 0 5 1 2 5 3 1 5 0 3 5; do le32 $i; done
} > deferred_mesh_bounds.povm
compare povm deferred_mesh_bounds Declare=Mode=2
compare povmtail eager_preparation Declare=Mode=5 Declare=Tail=2000000
test "$(line povmtail_on4)" = "Deferred meshes: 1 recorded, 0 built for parse-time queries, 1 built for the scene, 0 not needed." || { echo "eager preparation: povm counts $(line povmtail_on4)"; exit 1; }
eager povmtail_on4 | grep -q "1 of 1 scene meshes built ahead" || { echo "eager preparation: the povm mesh was not built ahead: $(eager povmtail_on4)"; exit 1; }
# a parse error ends the render with status 1, promptly
status=0; timeout -k 10 120 "$POVRAY" +i"$SRCDIR/tests/render/eager_preparation.pov" +L"$SRCDIR/include" +w8 +h8 -d -p -gp -f +wt4 \
    Declare=Mode=4 > eager_error.log 2>&1 || status=$?
test "$status" = 1 || { cat eager_error.log; echo "eager preparation: the error scene exited $status"; exit 1; }
grep -q "stop after the meshes are recorded" eager_error.log || { cat eager_error.log; echo "eager preparation: no parse error"; exit 1; }
# a stop sent once the log shows the marker ends the render as cancelled (status 2) within 30 s, before the eager builds finished
stop() {
    stop_name=$1; stop_scene=$2; stop_marker=$3; shift 3
    "$POVRAY" +i"$SRCDIR/tests/render/$stop_scene.pov" +L"$SRCDIR/include" +w8 +h8 -d -p -gp -f +wt4 "$@" > "eager_$stop_name.log" 2>&1 &
    stop_pid=$!
    stop_ticks=0
    until grep -qs "$stop_marker" "eager_$stop_name.log"; do
        kill -0 "$stop_pid" 2>/dev/null || { cat "eager_$stop_name.log"; echo "eager preparation: $stop_name ended before '$stop_marker'"; exit 1; }
        stop_ticks=$((stop_ticks + 1))
        test "$stop_ticks" -lt 6000 || { kill -9 "$stop_pid"; echo "eager preparation: $stop_name never printed '$stop_marker'"; exit 1; }
        sleep 0.02
    done
    kill -INT "$stop_pid"
    stop_ticks=0
    while kill -0 "$stop_pid" 2>/dev/null; do
        stop_ticks=$((stop_ticks + 1))
        test "$stop_ticks" -lt 300 || { kill -9 "$stop_pid"; cat "eager_$stop_name.log"; echo "eager preparation: $stop_name hung after the stop"; exit 1; }
        sleep 0.1
    done
    stop_status=0; wait "$stop_pid" || stop_status=$?
    test "$stop_status" = 2 && grep -q "Render cancelled by user" "eager_$stop_name.log" || { cat "eager_$stop_name.log"; echo "eager preparation: $stop_name exited $stop_status"; exit 1; }
    ! grep -q "Deferred meshes:\|Eager media grids:" "eager_$stop_name.log" || { cat "eager_$stop_name.log"; echo "eager preparation: $stop_name finished its builds first"; exit 1; }
}
stop parse eager_preparation "eager: recorded" Declare=Fine=1 Declare=Queries=0 Declare=Tail=100000000
stop wait eager_preparation "eager: parsed" Declare=Fine=1 Declare=Queries=0 Declare=Tail=0
stop mediaparse eager_media "eager media: recorded" Declare=Tail=100000000
rm -f eager_*.ppm eager_*.px eager_*.log deferred_mesh_bounds.povm deferred_mesh_bounds.povt
echo "eager preparation: same images and mesh counts as with it off, with redone builds, discards, skein, povm and media grids"
