#!/bin/sh
# A method 2 body without its own domain_cells asks a point per square of its material's diffusion length over its area, at
# most 1.73 a square millimetre; a body's own domain_cells is kept.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_body_density.pov" +w96 +h64 +wt2 +pr -a -d -p -f "$@" >"$name.log" 2>&1 ||
        { cat "$name.log"; exit 1; }
    # shape, area, domain_cells for each bounded body read
    tr '\n' ' ' <"$name.log" | tr -s '[:space:]' ' ' |
        grep -o 'Method 2 on the [a-z]* at <[^>]*> ([^)]*): area [^,]*, domain_cells [0-9]*' |
        sed 's/Method 2 on the \([a-z]*\) .*: area \([^,]*\), domain_cells \([0-9]*\)/\1 \2 \3/' >"$name.each"
}
fail() { cat "$name.log"; echo "subsurface_body_density: $*"; exit 1; }
# Each brick's surface is 0.0432 square units, 4.32 mm^2 at mm_per_unit 10: its wax diffuses shorter than the floor's
# 0.76 mm, so 1.73 points a mm^2 gives about 7.5, as the coarse map measures the area. The total is the density times the total area, to the rounding of each count.
name=wall; render wall
awk '$1 == "box" { n++; a += $2; c += $3; if ($2 < 0.03 || $2 > 0.05) bad=1 }
     $1 == "sphere" { s=$3 } $1 == "union" { u=$3 }
     END { d = c - 173 * a; if (d < 0) d = -d; exit (bad || n != 200 || d > 0.01 * 173 * a + n / 2 || s != 2000 || u != 500) }' wall.each ||
    fail 'the bricks did not take 1.73 points a square millimetre, or a stated budget was not kept'
# Twice the millimetres a unit is four times the area, so four times the points; the sphere's and the union's own are kept.
name=coarse; render coarse Declare=Mm=20
awk '$1 == "box" { n++; a += $2; c += $3 } $1 == "sphere" { s=$3 } $1 == "union" { u=$3 }
     END { d = c - 692 * a; if (d < 0) d = -d; exit (n != 200 || d > 0.01 * 692 * a + n / 2 || s != 2000 || u != 500) }' coarse.each ||
    fail 'the density did not follow mm_per_unit'
# Without their own, the sphere and the union are bodies at the same density: the union counts its two bricks' outer faces.
name=bare; render bare Declare=Bricks=0 Declare=Own=0 Declare=Joined=0
awk 'function off(a, c) { d = c - 173 * a; if (d < 0) d = -d; return d > 0.01 * 173 * a + 0.5 }
     $1 == "sphere" { s++; if ($2 < 0.95 || $2 > 1.25 || off($2, $3)) bad=1 }
     $1 == "union" { u++; if ($2 < 0.06 || $2 > 0.12 || off($2, $3)) bad=1 }
     END { exit (bad || s != 1 || u != 1) }' bare.each || fail 'a body without its own budget did not take the density over its area'
# A material that diffuses farther asks fewer points over the same sphere: a dark flesh at 0.4 mm below the floor, at 1.6 mm
# fewer still, and a white one at 0.4 mm far fewer than the dark. Each is n, area and floor count at mm_per_unit 40.
sphere() {
    name=$1; shift
    render "$name" Declare=Bricks=0 Declare=Joined=0 Declare=Mm=40 "$@"
    awk '$1 == "sphere" { print $3, $2, 1.73 * 1600 * $2 }' "$name.each"
}
dark=$(sphere dark Declare=Own=0 Declare=Colour=0.3 Declare=T=0.4)
long=$(sphere long Declare=Own=0 Declare=Colour=0.3 Declare=T=1.6)
white=$(sphere white Declare=Own=0 Declare=Colour=0.93 Declare=T=0.4)
snow=$(sphere snow Declare=Own=0 Declare=Colour=0.93 Declare=T=0.1)
pigment=$(sphere pigment Declare=Own=0 Declare=Colour=0.93 Declare=T=0.4 Declare=FleshMode=1)
colour=$(sphere colour Declare=Own=0 Declare=Colour=0.93 Declare=T=0.4 Declare=FleshMode=2)
[ "$pigment" = "$white" ] && [ "$colour" = "$white" ] ||
    { name=pigment; fail "constant flesh pigment and colour requested different densities: $pigment / $colour / $white"; }
echo "$dark $long $white $snow" | awk '{ exit !($1 > 0 && $1 < 0.9 * $3 && $4 < 0.5 * $1 && $7 < 0.1 * $1 && $10 >= 1 && $10 < 0.1 * $12) }' ||
    { name=snow; fail "the density did not follow the material's diffusion length (dark, long, white, snow: $dark / $long / $white / $snow)"; }
# The white flesh's own budget is kept, and a union with a wax brick takes the wax's floor (its shortest member's length).
name=mixed; render mixed Declare=Bricks=0 Declare=Mm=40 Declare=Colour=0.93 Declare=Mixed=1 Declare=Joined=0
awk '$1 == "sphere" { s=$3 } $1 == "union" { d = $3 - 1.73 * 1600 * $2; if (d < 0) d = -d; u = (d <= 0.01 * 1.73 * 1600 * $2 + 0.5) }
     END { exit !(s == 2000 && u) }' mixed.each || fail 'a stated budget was not kept, or a mixed union did not take its shortest member'
# There is no scene-wide or #default count.
cat >scene.pov <<'SCENE'
#default { subsurface { domain_cells 100 } }
SCENE
if "$POVRAY" +iscene.pov +w8 +h8 -d -p -f >default.log 2>&1; then
    name=default; fail '#default { subsurface { domain_cells } } was accepted'
fi
grep -q 'subsurface found instead' default.log || { name=default; fail '#default { subsurface } failed for another reason'; }
cat >scene.pov <<'SCENE'
global_settings { subsurface { method 2 domain_cells 5000 } }
SCENE
if "$POVRAY" +iscene.pov +w8 +h8 -d -p -f >global.log 2>&1; then
    name=global; fail 'global_settings { subsurface { domain_cells } } was accepted'
fi
grep -q 'undeclared identifier found' global.log || { name=global; fail 'global_settings domain_cells failed for another reason'; }
# An off-camera copy owns the shared map; stretching its visible sibling must still respect its point budget.
cat >copy.pov <<'SCENE'
#version 3.7;
#ifndef (Share) #declare Share = 1; #end
#ifndef (Cells) #declare Cells = 64; #end
global_settings { assumed_gamma 1 mm_per_unit 1 subsurface { method 2 samples 1, 1 } }
camera { orthographic location <0, 0, -20> look_at 0 right x*72 up y*4 }
light_source { <0, 1, -9>, rgb 1 }
#declare Body = sphere { 0, 1 pigment { rgb 0.6 }
  finish { diffuse 0.8 subsurface { translucency 0.3 } } interior { ior 1.4 }
  subsurface { domain_cells Cells } }
#if (Share) object { Body translate 20*y } #end
object { Body scale <32, 1, 1> }
SCENE
for share in 0 1; do
    for cells in 64 256; do
        name=copy${share}_${cells}
        "$POVRAY" +icopy.pov +w96 +h32 +wt2 +pr -a -d -p -f Declare=Share=$share Declare=Cells=$cells >"$name.log" 2>&1 || fail 'stretched copy failed'
        tr -d ' \r\n' <"$name.log" | sed -n 's/.*;[0-9]*cells,\([0-9]*\)pointsbuilt,.*/\1/p' >"$name.points"
        points=$(cat "$name.points")
        [ -n "$points" ] && [ "$points" -ge $((cells / 4)) ] && [ "$points" -le $((cells * 4)) ] ||
            fail "stretched copy built $points points for budget $cells (shared map $share)"
    done
    few=$(cat "copy${share}_64.points"); many=$(cat "copy${share}_256.points")
    [ "$many" -ge $((few * 2)) ] && [ "$many" -le $((few * 8)) ] || fail 'stretched copy did not scale with its point budget'
done
echo 'subsurface_body_density: ok'
