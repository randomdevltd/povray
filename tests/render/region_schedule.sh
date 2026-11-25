#!/bin/sh
# region_schedule.sh <povray> <srcdir>: under a Max_Cache_Memory the subsurface clouds do not fit, the coarse progressive
# levels record each tile's cells and the finer levels run cluster by cluster. The image is byte for byte the one line
# order renders, on 1, 4, 8 and 16 threads and in other block sizes and orders, with far fewer cells read back; a render
# stopped part way through the schedule continues to the same image, and so does one from a state file of format 0002.
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
# a state file of the previous format, levels whole, still continues
: > regionschedule_old.pov-state
ln regionschedule_old.pov-state regionschedule_oldwhole.pov-state
render old +wt4 $BUDGET Region_Schedule=off
head -c $(($(size regionschedule_oldwhole.pov-state) * 6 / 10)) regionschedule_oldwhole.pov-state > regionschedule_older.pov-state
printf '0002' | dd of=regionschedule_older.pov-state bs=1 seek=28 conv=notrunc 2> /dev/null
render older +wt4 $BUDGET +c
same plain older
if "$POVRAY" +i"$SRCDIR/tests/render/region_schedule.pov" +w16 +h16 -d -p -v -f Region_Schedule=maybe 2> /dev/null; then
    fail "Region_Schedule=maybe should be refused"
fi
echo "region_schedule: line order reads back $lineMiB MiB of cells, the schedule $autoMiB MiB, same image on 1, 4, 8 and 16 threads, continued from a cut state file and from format 0002"
