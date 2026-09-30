#!/bin/sh
# antialias_m5.sh <povray> <srcdir>: +AM5 draws the same image on any thread count, anti-aliases an edge, and needs +PR.
set -e
W=160; H=120
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/antialias_m4.pov" +L"$SRCDIR/include" +w$W +h$H -d -p -v -gp +fp +o"am5_$name.ppm" Declare=Mode=2 "$@"
}
pixels() {
    tail -c $((W * H * 3)) "am5_$1.ppm" > "am5_$1.px"
}
same() {
    pixels "$1"; pixels "$2"
    cmp -s "am5_$1.px" "am5_$2.px"
}
POVRAY=$1; SRCDIR=$2
render none +pr -a
render one +pr +am5 +a0.02 +ab0.5 +wt1
render four +pr +am5 +a0.02 +ab0.5 +wt4
same one four || { echo "antialias_m5: the image depends on the thread count"; exit 1; }
if same none one; then
    echo "antialias_m5: the disc's edge was not anti-aliased"; exit 1
fi
if render refused +am5 +a0.02 2> /dev/null; then
    echo "antialias_m5: +AM5 without +PR should be refused"; exit 1
fi
rm -f am5_*.ppm am5_*.px
echo "antialias_m5: +AM5 anti-aliases an edge, the same on any thread count"
