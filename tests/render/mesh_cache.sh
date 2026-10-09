#!/bin/sh
# mesh_cache.sh <povray> <srcdir>: generated meshes are stored on the first render, reused unchanged on the next, rebuilt when an input changes.
set -e
POVRAY=$1; SRCDIR=$2
rm -rf mesh_cache_dir && mkdir mesh_cache_dir
run() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/mesh_cache_check.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w48 +h36 -a -d -p -v -gp +wt1 +fp +o"mesh_cache_$name.ppm" "$@" > "mesh_cache_$name.log" 2>&1 || { cat "mesh_cache_$name.log"; exit 1; }
    tail -c $((48 * 36 * 3)) "mesh_cache_$name.ppm" > "mesh_cache_$name.px"
}
cached() { grep -c '(cached)' "mesh_cache_$1.log" || true; }
run plain
run first Declare=Cache=1
run second Declare=Cache=1
run changed Declare=Cache=1 Declare=Amp=0.25
test "$(ls mesh_cache_dir | grep -c '\.povg$')" = 4 || { ls mesh_cache_dir; echo "mesh cache: not two meshes per input set"; exit 1; }
test "$(cached first)" = 0 && test "$(cached second)" = 2 && test "$(cached changed)" = 0 || { echo "mesh cache: hits were $(cached first), $(cached second), $(cached changed); expected 0, 2, 0"; exit 1; }
cmp -s mesh_cache_plain.px mesh_cache_first.px && cmp -s mesh_cache_plain.px mesh_cache_second.px || { echo "mesh cache: a cached mesh renders differently"; exit 1; }
! cmp -s mesh_cache_plain.px mesh_cache_changed.px || { echo "mesh cache: a changed input reused the old mesh"; exit 1; }
rm -rf mesh_cache_dir mesh_cache_*.ppm mesh_cache_*.px mesh_cache_*.log
echo "mesh cache: stored, reused pixel for pixel, and rebuilt when an input changed"
