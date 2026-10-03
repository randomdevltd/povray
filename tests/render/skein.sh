#!/bin/sh
# skein.sh <povray> <srcdir>: skein shapes match their primitives in hits, normals and inside(), and render pixel for pixel the same.
set -e
POVRAY=$1; SRCDIR=$2
W=96; H=72
"$POVRAY" +i"$SRCDIR/tests/render/skein_check.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_keywords.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_syntax.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_crease.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_values.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_envelope.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_csg.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_sample_path.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_bend.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_crease_axis.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_curl.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
"$POVRAY" +i"$SRCDIR/tests/render/skein_blend.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f
# boxes past a crease's arc must shrink with the patch: unresolved patches stay in the hundreds (300000 without that)
"$POVRAY" +i"$SRCDIR/tests/render/skein_crease_tail.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w24 +h24 -d -p -v -gp -f +wt1 > skein_crease_tail.log 2>&1 || { cat skein_crease_tail.log; exit 1; }
unresolved=$(sed -n 's/^Skein unresolved: *//p' skein_crease_tail.log)
echo "skein_crease_tail: ${unresolved:-no statistics} unresolved patches"
test "${unresolved:-1000000}" -lt 5000
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/skein_shapes.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w$W +h$H -a -d -p -v -gp +wt1 +fp +o"skein_$name.ppm" "$@"
    # the header carries the render date, so only the pixels are compared
    tail -c $((W * H * 3)) "skein_$name.ppm" > "skein_$name.px"
}
for case in 1:0 2:0 3:0 4:0 5:0 1:1 3:1 7:0 8:0 9:0 10:0 11:0 11:1 13:0 14:0 15:0 16:0 21:0 22:0 23:0 26:0 27:0 28:0 29:0; do
    shape=${case%:*}; cut=${case#*:}
    render skein Declare=Shape=$shape Declare=Cut=$cut
    render prim Declare=Shape=$shape Declare=Cut=$cut Declare=Prim=1
    if ! cmp -s skein_skein.px skein_prim.px; then
        echo "skein: shape $shape (cutaway $cut) renders differently from its primitive"; exit 1
    fi
done
render twisted Declare=Shape=5 Declare=Checker=1
render straight Declare=Shape=2 Declare=Checker=1
if cmp -s skein_twisted.px skein_straight.px; then
    echo "skein: a twist leaves the uv checker unturned"; exit 1
fi
rm -f skein_*.ppm skein_*.px
echo "skein: shapes match their primitives in hits, normals and inside, and render the same"
