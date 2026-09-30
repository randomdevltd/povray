#!/bin/sh
# antialias_m4.sh <povray> <srcdir>: +AM4 keeps one-pixel detail and grain, averages grain with -AKG, and continues where it stopped.
set -e
W=40; H=30
render() {
    name=$1; mode=$2; shift 2
    "$POVRAY" +i"$SRCDIR/tests/render/antialias_m4.pov" +L"$SRCDIR/include" +w$W +h$H -d -p -v -gp +fp +o"am4_$name.ppm" Declare=Mode=$mode "$@"
}
pixels() {
    tail -c $((W * H * 3)) "am4_$1.ppm" > "am4_$1.px"
}
size() {
    if [ -f "$1" ]; then echo $(($(wc -c < "$1"))); else echo 0; fi
}
same() {
    pixels "$1"; pixels "$2"
    cmp -s "am4_$1.px" "am4_$2.px"
}
POVRAY=$1; SRCDIR=$2
render checker 0 +pr +am4 +a0.02 +r3
pixels checker
if [ "$(od -An -v -tu1 am4_checker.px | tr -s ' ' '\n' | grep -cvE '^(0|255|)$')" != 0 ]; then
    echo "antialias_m4: a checkerboard of one-pixel squares lost contrast"; exit 1
fi
render grain_none 1 +pr -a
render grain_kept 1 +pr +am4 +a0.02 +r3
same grain_none grain_kept || { echo "antialias_m4: grain alone was anti-aliased"; exit 1; }
render grain_averaged 1 +pr +am4 +a0.02 +r3 -akg
if same grain_none grain_averaged; then
    echo "antialias_m4: with -AKG grain should be averaged"; exit 1
fi
if render refused 2 +am4 +a0.02 2> /dev/null; then
    echo "antialias_m4: +AM4 without +PR should be refused"; exit 1
fi
W=160; H=120
# a second link keeps the whole render's state; its refinement pass stores 5 floats a pixel, so cutting 10 bytes a pixel stops it halfway
: > am4_whole.pov-state
ln am4_whole.pov-state am4_copy.pov-state
render copy 2 +pr +am4 +a0.02 +r3 +wt1
mv am4_copy.ppm am4_whole.ppm
head -c $(($(size am4_whole.pov-state) - 10 * W * H)) am4_whole.pov-state > am4_resumed.pov-state
render resumed 2 +pr +am4 +a0.02 +r3 +wt1 +c > am4_resumed.log 2>&1 || { cat am4_resumed.log; exit 1; }
grep -q "^Pixels: *0 " am4_resumed.log || { echo "antialias_m4: the continued render traced lattice samples again"; exit 1; }
same whole resumed || { echo "antialias_m4: the continued render differs"; exit 1; }
rm -f am4_*.ppm am4_*.px am4_*.log am4_*.pov-state*
echo "antialias_m4: +AM4 keeps one-pixel detail and grain, and continues where it stopped"
