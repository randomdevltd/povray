#!/bin/sh
# antialias_m4.sh <povray> <srcdir>: +AM4 draws the same image on any thread count, anti-aliases an edge, and needs +PR.
set -e
W=160; H=120
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/antialias_m4.pov" +L"$SRCDIR/include" +w$W +h$H -d -p -v -gp +fp +o"am4_$name.ppm" "$@"
}
pixels() {
    tail -c $((W * H * 3)) "am4_$1.ppm" > "am4_$1.px"
}
same() {
    pixels "$1"; pixels "$2"
    cmp -s "am4_$1.px" "am4_$2.px"
}
POVRAY=$1; SRCDIR=$2
render none +pr -a
render one +pr +am4 +a0.02 +ab0.5 +wt1
render four +pr +am4 +a0.02 +ab0.5 +wt4
same one four || { echo "antialias_m4: the image depends on the thread count"; exit 1; }
if same none one; then
    echo "antialias_m4: the disc's edge was not anti-aliased"; exit 1
fi
if render refused +am4 +a0.02 2> /dev/null; then
    echo "antialias_m4: +AM4 without +PR should be refused"; exit 1
fi
rm -f am4_*.ppm am4_*.px
echo "antialias_m4: +AM4 anti-aliases an edge, the same on any thread count"
