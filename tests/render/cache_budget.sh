#!/bin/sh
# cache_budget.sh <povray> <srcdir>: under Max_Cache_Memory subsurface cells and media grids spill to disk and come back
# with the image unchanged, on any thread count and from POV_CACHE_MEMORY too; without a budget nothing spills or is
# logged; pinned caches past the budget stop the render before its first pixel naming them; a budget that is not a
# whole number of MB greater than zero is refused.
set -e
trap 'rm -rf cachebudget_*' EXIT
POVRAY=$1; SRCDIR=$2
SCENE=$SRCDIR/tests/render/cache_budget.pov
W=64; H=48
render() {
    name=$1; shift
    "$POVRAY" +i"$SCENE" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 +o"cachebudget_$name.ppm" "$@" > "cachebudget_$name.log" 2>&1
}
text() { tr -d ' \r\n' < "cachebudget_$1.log" > "cachebudget_$1.txt"; }
pixels() { tail -c $((6 * W * H)) "cachebudget_$1.ppm" > "cachebudget_$1.px"; }
fail() { echo "cache_budget: $*"; exit 1; }
has() { grep -q -- "$2" "cachebudget_$1.txt"; }

render plain +wt4 || { cat cachebudget_plain.log; fail "the scene did not render without a budget"; }
text plain; pixels plain
has plain 'Cachememory' && fail "a render without a budget logged cache memory"

# 8 MB is far below the clouds and grids, so both spill and are read back
render tight +wt4 Max_Cache_Memory=8 || { cat cachebudget_tight.log; fail "the scene did not render under an 8 MB budget"; }
text tight; pixels tight
cmp -s cachebudget_plain.px cachebudget_tight.px || fail "spilling changed the image"
has tight 'Cachememory:subsurfaceclouds:peak[0-9.]*MiBresident;spilled[0-9.]*MiBin[1-9][0-9]*writes,readback[0-9.]*MiBin[1-9][0-9]*reads' ||
    { cat cachebudget_tight.log; fail "subsurface cells were not spilled and read back"; }
has tight 'Cachememory:mediagrids:peak[0-9.]*MiBresident;spilled[0-9.]*MiBin[1-9]' || { cat cachebudget_tight.log; fail "media grids were not spilled"; }
has tight 'Cachememory:budget8.0MiB(Max_Cache_Memory)' || fail "the budget line is missing"

render one +wt1 Max_Cache_Memory=8 || { cat cachebudget_one.log; fail "the scene did not render on one thread under a budget"; }
pixels one
cmp -s cachebudget_plain.px cachebudget_one.px || fail "one thread under a budget rendered differently"

(POV_CACHE_MEMORY=8; export POV_CACHE_MEMORY; render env +wt2) || { cat cachebudget_env.log; fail "POV_CACHE_MEMORY did not render"; }
text env; pixels env
cmp -s cachebudget_plain.px cachebudget_env.px || fail "POV_CACHE_MEMORY changed the image"
has env 'Cachememory:budget8.0MiB' || fail "POV_CACHE_MEMORY set no budget"

# an ample budget spills nothing
render ample +wt4 Max_Cache_Memory=4096 || { cat cachebudget_ample.log; fail "the scene did not render under an ample budget"; }
text ample; pixels ample
cmp -s cachebudget_plain.px cachebudget_ample.px || fail "an ample budget changed the image"
has ample 'spilled' && fail "an ample budget spilled"

# thousands of spheres fill the bounding tree, which cannot spill: past a 1 MB budget the render stops before its first pixel
if render pinned +wt2 Max_Cache_Memory=1 Declare=Blocks=0 Declare=MediaCells=0 Declare=Spheres=40000; then fail "pinned caches past the budget rendered"; fi
text pinned
has pinned 'Max_Cache_Memoryis1.0MiB,butthecachesthatcannotbedividedandspilledneed[0-9.]*MiBontheirown:scenebounding' ||
    { cat cachebudget_pinned.log; fail "the pinned caches past the budget were not named"; }
has pinned 'Rendered[1-9]' && fail "a render whose pinned caches pass the budget traced pixels"

for value in 0 -5 1.5 abc; do
    if render "bad_$value" Max_Cache_Memory="$value" Declare=Blocks=1; then fail "Max_Cache_Memory=$value was accepted"; fi
    text "bad_$value"
    has "bad_$value" 'Max_Cache_Memorymustbeawholenumberof' || { cat "cachebudget_bad_$value.log"; fail "Max_Cache_Memory=$value was not refused by name"; }
done
if (POV_CACHE_MEMORY=abc; export POV_CACHE_MEMORY; render bad_env Declare=Blocks=1); then fail "POV_CACHE_MEMORY=abc was accepted"; fi
text bad_env
has bad_env 'POV_CACHE_MEMORYmustbeawholenumberof' || { cat cachebudget_bad_env.log; fail "POV_CACHE_MEMORY=abc was not refused by name"; }
echo "cache_budget: subsurface cells and media grids spill and come back with the same image on 1 and 4 threads and from POV_CACHE_MEMORY; no budget logs nothing; an ample budget spills nothing; pinned overflow named before the first pixel; bad budgets refused"
