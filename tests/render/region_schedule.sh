#!/bin/sh
# region_schedule.sh <povray> <srcdir>: under a Max_Cache_Memory the subsurface clouds do not fit, the coarse progressive
# levels record each tile's cells and the finer levels run cluster by cluster. The image is byte for byte the one line
# order renders, on 1, 4, 8 and 16 threads, in other block sizes and orders and anti-aliased, with far fewer cells read back.
# A render stopped part way through the schedule continues to the same image with the schedule or without it, and the
# state file is 0003 only once a schedule is written to it.
set -e
trap 'rm -f regionschedule_*' EXIT
POVRAY=$1; SRCDIR=$2
W=320; H=200
BUDGET=Max_Cache_Memory=16
fail() { echo "region_schedule: $*" >&2; exit 1; }
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/region_schedule.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1.0 Declare=Cells=8000 \
        +o"regionschedule_$name.ppm" "$@" > "regionschedule_$name.log" 2>&1 || { cat "regionschedule_$name.log"; fail "$name failed"; }
    tail -c $((6 * W * H)) "regionschedule_$name.ppm" > "regionschedule_$name.px"
}
same() {
    cmp -s "regionschedule_$1.px" "regionschedule_$2.px" || fail "$2 differs from $1"
}
logged() {
    tr -s ' \r\n' ' ' < "regionschedule_$1.log" | grep -q "$2" || { cat "regionschedule_$1.log"; fail "$1 does not log '$2'"; }
}
# MiB of subsurface cells read back from the spill file
readback() {
    tr -d ' \r\n' < "regionschedule_$1.log" | sed -n 's/.*Cachememory:subsurfaceclouds:[^.]*\.[0-9]*MiBresident;spilled[0-9.]*MiBin[0-9]*writes,readback\([0-9.]*\)MiB.*/\1/p'
}
render plain +wt4
render line +wt4 $BUDGET Region_Schedule=off
render auto +wt4 $BUDGET
same plain line
same plain auto
! grep -q "Region schedule" regionschedule_line.log || fail "Region_Schedule=off logs a schedule"
logged auto "Region schedule: [0-9]* tiles of [0-9]* blocks in [0-9]* clusters"
logged auto "Region schedule: footprint misses"
lineMiB=$(readback line); autoMiB=$(readback auto)
[ -n "$lineMiB" ] && [ -n "$autoMiB" ] || fail "no read-back figures: line '$lineMiB', scheduled '$autoMiB'"
awk -v l="$lineMiB" -v a="$autoMiB" 'BEGIN { exit !(a * 2 < l) }' || fail "scheduled read back $autoMiB MiB, line order $lineMiB MiB"
for run in "one +wt1" "eight +wt8 +bs13 +rp3" "sixteen +wt16 +bs5"; do
    set -- $run; name=$1; shift
    render "$name" $BUDGET "$@"
    same plain "$name"
    mib=$(readback "$name")
    awk -v l="$lineMiB" -v a="$mib" 'BEGIN { exit !(a * 2 < l) }' || fail "$name read back $mib MiB, line order $lineMiB MiB"
    logged "$name" "Region schedule: [0-9]* tiles of [0-9]* blocks in [0-9]* clusters"
done
# an ample budget needs no schedule, unless asked for, when it is one cluster
render fits +wt4 Max_Cache_Memory=4096
logged fits "Region schedule: not needed"
render forced +wt4 Max_Cache_Memory=4096 Region_Schedule=on
logged forced "blocks in 1 clusters"
same plain forced

# a state file cut part way into the scheduled levels: the continued render follows the saved schedule
size() { if [ -f "$1" ]; then echo $(($(wc -c < "$1"))); else echo 0; fi; }
: > regionschedule_cut.pov-state
ln regionschedule_cut.pov-state regionschedule_whole.pov-state
render cut +wt4 $BUDGET
head -c $(($(size regionschedule_whole.pov-state) * 7 / 10)) regionschedule_whole.pov-state > regionschedule_resumed.pov-state
render resumed +wt4 $BUDGET +c
same plain resumed
logged resumed "Region schedule: continued in the saved order"
# the state file's version: 0002 while levels run whole, 0003 once a schedule is written to it
version() { dd if="$1" bs=1 skip=28 count=4 2> /dev/null; }
[ "$(version regionschedule_whole.pov-state)" = 0003 ] || fail "a scheduled render's state file is not 0003"
: > regionschedule_cut13.pov-state
ln regionschedule_cut13.pov-state regionschedule_whole13.pov-state
render cut13 +wt4 +bs13 $BUDGET
for name in offresumed nobudget; do
    head -c $(($(size regionschedule_whole13.pov-state) * 7 / 10)) regionschedule_whole13.pov-state > "regionschedule_$name.pov-state"
done
# continued with the schedule off or no budget, off the 16-pixel lattice, the levels run whole and keep finished tiles' pixels
render offresumed +wt4 +bs13 $BUDGET Region_Schedule=off +c
same plain offresumed
render nobudget +wt4 +bs13 +c
same plain nobudget
# a render without a schedule keeps 0002, which older builds read; continued past the coarse levels it stays so
: > regionschedule_old.pov-state
ln regionschedule_old.pov-state regionschedule_oldwhole.pov-state
render old +wt4 $BUDGET Region_Schedule=off
[ "$(version regionschedule_oldwhole.pov-state)" = 0002 ] || fail "a render without a schedule did not write 0002"
head -c $(($(size regionschedule_oldwhole.pov-state) * 6 / 10)) regionschedule_oldwhole.pov-state > regionschedule_older.pov-state
ln regionschedule_older.pov-state regionschedule_olderkept.pov-state
render older +wt4 $BUDGET +c
same plain older
[ "$(version regionschedule_olderkept.pov-state)" = 0002 ] || fail "a 0002 file continued without a schedule became $(version regionschedule_olderkept.pov-state)"
# continued inside the coarse levels, the footprints would be partial, so it is not scheduled
head -c 20000 regionschedule_oldwhole.pov-state > regionschedule_coarse.pov-state
ln regionschedule_coarse.pov-state regionschedule_coarsekept.pov-state
render coarse +wt4 $BUDGET +c
same plain coarse
logged coarse "continued inside the coarse passes"
[ "$(version regionschedule_coarsekept.pov-state)" = 0002 ] || fail "a render continued inside the coarse levels marked its file 0003"
# a 0002 file holding only its header is continued from the start, scheduled, and marked 0003 before the schedule
head -c 512 regionschedule_oldwhole.pov-state > regionschedule_fresh.pov-state
ln regionschedule_fresh.pov-state regionschedule_freshkept.pov-state
render fresh +wt4 $BUDGET +c
same plain fresh
logged fresh "Region schedule: [0-9]* tiles of"
[ "$(version regionschedule_freshkept.pov-state)" = 0003 ] || fail "a 0002 file given a schedule stayed $(version regionschedule_freshkept.pov-state)"
# anti-aliasing passes run in the schedule's order: the same image as line order, method 4 and method 1
for aa in "m4 +a0.1 +am4" "m1 +a0.1 +am1"; do
    set -- $aa; method=$1; shift
    render "aa$method" +wt4 "$@"
    render "aaline$method" +wt4 $BUDGET Region_Schedule=off "$@"
    render "aasched$method" +wt4 $BUDGET "$@"
    same "aa$method" "aaline$method"
    same "aa$method" "aasched$method"
    logged "aasched$method" "Region schedule: [0-9]* tiles of"
done
if "$POVRAY" +i"$SRCDIR/tests/render/region_schedule.pov" +w16 +h16 -d -p -v -f Region_Schedule=maybe 2> /dev/null; then
    fail "Region_Schedule=maybe should be refused"
fi
echo "region_schedule: line order reads back $lineMiB MiB of cells, the schedule $autoMiB MiB, same image on 1, 4, 8 and 16 threads and anti-aliased by methods 1 and 4; continued from a cut state file, with the schedule off or no budget at +bs13, inside the coarse levels and from format 0002, which stays 0002 unless a schedule is written"
