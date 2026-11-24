#!/bin/sh
# cache_budget_threads.sh <povray> <srcdir>: stacked see-through subsurface bricks whose clouds spill and come back under
# a tiny Max_Cache_Memory render byte for byte the same as without a budget, on 1, 2, 4, 8 and 16 threads and in any
# block size and order, so what a thread traced before and what the budget spilled never change a pixel.
set -e
trap 'rm -f cachebudgetthreads_*' EXIT
POVRAY=$1; SRCDIR=$2
W=160; H=120
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/cache_budget_threads.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"cachebudgetthreads_$name.ppm" "$@" \
        > "cachebudgetthreads_$name.log" 2>&1 || { cat "cachebudgetthreads_$name.log"; echo "cache_budget_threads: $name failed" >&2; exit 1; }
    tail -c $((6 * W * H)) "cachebudgetthreads_$name.ppm" > "cachebudgetthreads_$name.px"
}
same() {
    cmp -s "cachebudgetthreads_$1.px" "cachebudgetthreads_$2.px" || { echo "cache_budget_threads: $2 differs from $1" >&2; exit 1; }
}
spilled() {
    tr -d ' \r\n' < "cachebudgetthreads_$1.log" | grep -q 'Cachememory:subsurfaceclouds:peak[0-9.]*MiBresident;spilled[0-9.]*MiBin[1-9][0-9]*writes,readback[0-9.]*MiBin[1-9][0-9]*reads' ||
        { cat "cachebudgetthreads_$1.log"; echo "cache_budget_threads: $1 spilled no cells" >&2; exit 1; }
}
render plain +wt1
render plain16 +wt16 +bs4 +rp3
same plain plain16
# 4 MB holds the pinned surface maps and a few cells, so nearly every cell is spilled and read back many times
for run in "one +wt1" "two +wt2 +bs16" "four +wt4 +bs8" "eight +wt8 +bs5 +rp5" "sixteen +wt16 +bs4 +rp3"; do
    set -- $run; name=$1; shift
    render "$name" Max_Cache_Memory=4 "$@"
    spilled "$name"
    same plain "$name"
done
echo "cache_budget_threads: spilled clouds render the unbound image on 1, 2, 4, 8 and 16 threads in any block order"
