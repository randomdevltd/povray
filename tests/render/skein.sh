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
for check in 0 1 2 3 6 7 8; do
    "$POVRAY" +i"$SRCDIR/tests/render/mesh_screen_context.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f Declare=Check=$check
done
for check in 4 5 9 10 11 12; do
    if "$POVRAY" +i"$SRCDIR/tests/render/mesh_screen_context.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f Declare=Check=$check > mesh_screen.log 2>&1; then
        cat mesh_screen.log; echo "mesh screen sample: parse unexpectedly succeeded"; exit 1
    fi
    grep -q "cannot sample a screen pigment" mesh_screen.log || { cat mesh_screen.log; exit 1; }
done
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
# skein_mesh: six meshes, each closed; the torus at max_angle 10 is 64 by 64 cells, built once though used four times
"$POVRAY" +i"$SRCDIR/tests/render/skein_mesh_check.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f > skein_mesh.log 2>&1 || { cat skein_mesh.log; exit 1; }
grep -q "^skein_mesh: 8192 triangles, 4225 vertices, 0 open edges" skein_mesh.log || { cat skein_mesh.log; echo "skein_mesh: the torus is not 8192 triangles on 4225 vertices, closed"; exit 1; }
test "$(grep -c '^skein_mesh: .* 0 open edges' skein_mesh.log)" = 6 || { cat skein_mesh.log; echo "skein_mesh: not six closed meshes"; exit 1; }
"$POVRAY" +i"$SRCDIR/tests/render/deferred_mesh_check.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f +wt4 > deferred_mesh.log 2>&1 || { cat deferred_mesh.log; exit 1; }
test "$(grep -c '^skein_mesh: .* 0 open edges' deferred_mesh.log)" = 2 || { cat deferred_mesh.log; echo "deferred mesh: redeclaration did not build twice"; exit 1; }
# a pending .povm octahedron in a translated merge or unsplit union must not be cut to the box taken before it loaded
le32() { printf "\\$(printf %03o $(($1 & 255)))\\$(printf %03o $(($1 >> 8 & 255)))\\$(printf %03o $(($1 >> 16 & 255)))\\$(printf %03o $(($1 >> 24 & 255)))"; }
{
    printf POVM; for n in 1 0 6 0 0 8; do le32 $n; done
    for v in 1065353216 0 0 3212836864 0 0 0 1065353216 0 0 3212836864 0 0 0 1065353216 0 0 3212836864; do le32 $v; done
    for i in 0 2 4 2 1 4 1 3 4 3 0 4 2 0 5 1 2 5 3 1 5 0 3 5; do le32 $i; done
} > deferred_mesh_bounds.povm
for mode in 0 1 2; do
    "$POVRAY" +i"$SRCDIR/tests/render/deferred_mesh_bounds.pov" +L. +w64 +h48 -a -d -p -v -gp +wt4 +fp Declare=Mode=$mode +o"deferred_bounds_$mode.ppm" > deferred_bounds.log 2>&1 || { cat deferred_bounds.log; exit 1; }
    tail -c $((64 * 48 * 3)) "deferred_bounds_$mode.ppm" > "deferred_bounds_$mode.px"
done
cmp -s deferred_bounds_0.px deferred_bounds_2.px && cmp -s deferred_bounds_1.px deferred_bounds_2.px || { echo "deferred mesh: a merge or union clipped its pending mesh"; exit 1; }
rm -f deferred_mesh_bounds.povm deferred_mesh_bounds.povt deferred_bounds_*
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
