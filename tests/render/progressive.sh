#!/bin/sh
# progressive.sh <povray> <srcdir>: +PR renders what block order renders, and a continued +PR render what an uninterrupted one does.
set -e
W=67; H=43
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/random_effects.pov" +L"$SRCDIR/include" +w$W +h$H -d -p -v -gp +fp16 +o"progressive_$name.ppm" "$@"
}
pixels() {
    tail -c $((W * H * 6)) "progressive_$1.ppm" > "progressive_$1.px"
}
size() {
    if [ -f "$1" ]; then echo $(($(wc -c < "$1"))); else echo 0; fi
}
same() {
    pixels "$1"; pixels "$2"
    cmp "progressive_$1.px" "progressive_$2.px"
}
POVRAY=$1; SRCDIR=$2
render rows -a +wt4
render levels -a +wt4 +pr
same rows levels
render rows_am2 +a0.1 +am2 +r2 +wt4
render levels_am2 +a0.1 +am2 +r2 +wt4 +pr
same rows_am2 levels_am2
W=160; H=120
# a second link keeps the whole render's state; its anti-aliasing pass stores 5 floats a pixel, so cutting 10 bytes a pixel stops it halfway
: > progressive_levels_am1.pov-state
ln progressive_levels_am1.pov-state progressive_whole.pov-state
render levels_am1 +a0.1 +am1 +wt1 +pr
head -c $(($(size progressive_whole.pov-state) - 10 * W * H)) progressive_whole.pov-state > progressive_resumed.pov-state
render resumed +a0.1 +am1 +wt1 +pr +c > progressive_resumed.log 2>&1 || { cat progressive_resumed.log; exit 1; }
if ! grep -q "^Pixels: *0 " progressive_resumed.log; then
    echo "progressive: the continued render traced lattice samples again"; exit 1
fi
same levels_am1 resumed
if "$POVRAY" +i"$SRCDIR/tests/render/random_effects.pov" +L"$SRCDIR/include" +w$W +h$H -d -p -v -gp -f +a0.1 +am3 +pr 2> /dev/null; then
    echo "progressive: +AM3 with +PR should be refused"; exit 1
fi
rm -f progressive_*.ppm progressive_*.px progressive_*.log progressive_*.pov-state*
echo "progressive: +PR matches block order, and continues where it stopped"
