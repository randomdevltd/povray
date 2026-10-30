#!/bin/sh
# antialias_m4.sh <povray> <srcdir>: +AM4 draws the same image on any thread count and when continued, anti-aliases an edge, and needs +PR.
set -e
W=160; H=120
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/antialias_m4.pov" +L"$SRCDIR/distribution/include" +w$W +h$H -d -p -v -gp +fp +o"am4_$name.ppm" "$@"
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
# a continued render restores the centre samples' edge map with them: cut inside the resolve, and inside the last lattice level
size() {
    if [ -f "$1" ]; then echo $(($(wc -c < "$1"))); else echo 0; fi
}
for scene in antialias_m4 random_effects; do
    : > am4_whole.pov-state
    ln -f am4_whole.pov-state am4_keep.pov-state
    render whole +i"$SRCDIR/tests/render/$scene.pov" +pr +am4 +a0.02 +ab1 +wt2
    for cut in 10 30; do
        head -c $(($(size am4_keep.pov-state) - cut * W * H)) am4_keep.pov-state > am4_resumed.pov-state
        rm -f am4_resumed.ppm
        render resumed +i"$SRCDIR/tests/render/$scene.pov" +pr +am4 +a0.02 +ab1 +wt2 +c > am4_resumed.log 2>&1 || { cat am4_resumed.log; exit 1; }
        same whole resumed || { echo "antialias_m4: $scene continued from $cut bytes a pixel short differs from the whole render"; exit 1; }
    done
done
if render refused +am4 +a0.02 2> /dev/null; then
    echo "antialias_m4: +AM4 without +PR should be refused"; exit 1
fi
rm -f am4_*.ppm am4_*.px am4_*.pov-state* am4_*.log
echo "antialias_m4: +AM4 anti-aliases an edge, the same on any thread count and when continued"
