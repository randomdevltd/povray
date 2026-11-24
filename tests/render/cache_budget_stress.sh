#!/bin/sh
# cache_budget_stress.sh <povray> <srcdir>: at 16 threads under a 4 MB Max_Cache_Memory, large hot cells, many small
# spilled ones, gathers whose ring spans more than three cells a side and many media grids too small to spill hold
# their caches under the ceiling the render logs, with the unbound image; small grids past a 1 MB budget stop the render
# before its first pixel, named.
set -e
trap 'rm -rf cachebudgetstress_*' EXIT
POVRAY=$1; SRCDIR=$2
W=128; H=16
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/cache_budget_stress.pov" +w$W +h$H +pr -a -d -p -v -gp +tf +fp16 File_Gamma=1 \
        +o"cachebudgetstress_$name.ppm" "$@" > "cachebudgetstress_$name.log" 2>&1
}
fail() { echo "cache_budget_stress: $*"; exit 1; }
text() { tr -d ' \r\n' < "cachebudgetstress_$1.log" > "cachebudgetstress_$1.txt"; }
field() { sed -n "s/.*$2.*/\1/p" "cachebudgetstress_$1.txt"; }

render plain +wt16 || { cat cachebudgetstress_plain.log; fail "the scene did not render without a budget"; }
render tight +wt16 Max_Cache_Memory=4 || { cat cachebudgetstress_tight.log; fail "the scene did not render under a 4 MB budget"; }
text tight
tail -c $((6 * W * H)) cachebudgetstress_plain.ppm > cachebudgetstress_plain.px
tail -c $((6 * W * H)) cachebudgetstress_tight.ppm > cachebudgetstress_tight.px
cmp -s cachebudgetstress_plain.px cachebudgetstress_tight.px || fail "the budget changed the image"
grep -q 'Cachememory:subsurfaceclouds:peak[0-9.]*MiBresident;spilled[0-9.]*MiBin[1-9][0-9]*writes,readback[0-9.]*MiBin[1-9][0-9]*reads' \
    cachebudgetstress_tight.txt || { cat cachebudgetstress_tight.log; fail "cells were not spilled and read back"; }

held=$(field tight 'MiBofcachesresidentwithmappedpages,\([0-9.]*\)MiBheld')
ceiling=$(field tight 'ceilingonheldcaches\([0-9.]*\)MiBfor16threads')
pins=$(field tight 'mostonethreadpinned[0-9.]*MiBin\([0-9]*\)cells')
unspillable=$(field tight "unspillablecaches'peak\([0-9.]*\)MiB")
grids=$(field tight 'Cachememory:mediagrids:peak\([0-9.]*\)MiBresident')
echo "cache_budget_stress: held $held MiB, ceiling $ceiling MiB, most cells one thread pinned $pins, unspillable $unspillable MiB, media grids $grids MiB"
# a ring past a cell pins more than two nested gathers of 27 could
awk -v n="$pins" 'BEGIN { exit !(n > 54) }' || { cat cachebudgetstress_tight.log; fail "no gather's ring spanned more than three cells a side ($pins pinned)"; }
# the media grids are too small to spill, so they count as unspillable
awk -v u="$unspillable" -v g="$grids" 'BEGIN { exit !((g > 1) && (u >= g)) }' || { cat cachebudgetstress_tight.log; fail "the small media grids were not counted unspillable"; }
awk -v h="$held" -v c="$ceiling" 'BEGIN { exit !((h > 4) && (h <= c)) }' ||
    { cat cachebudgetstress_tight.log; fail "16 threads held $held MiB of caches, past the logged ceiling of $ceiling MiB"; }

# grids that stay on the heap past the budget fail before the first pixel, named
if render small +wt4 Max_Cache_Memory=1; then fail "small grids past the budget rendered"; fi
text small
grep -q 'butthecachesthatcannotbedividedandspilledneed[0-9.]*MiBontheirown:.*gridsandlatticesunder1.0MiBeach[0-9.]*MiB' cachebudgetstress_small.txt ||
    { cat cachebudgetstress_small.log; fail "small grids past the budget were not named"; }
grep -q 'Rendered[1-9]' cachebudgetstress_small.txt && fail "a render whose small grids pass the budget traced pixels"
echo "cache_budget_stress: hot and small cells, rings wider than a cell and small grids hold 16 threads under the logged ceiling with the unbound image; small grids past the budget named before the first pixel"
