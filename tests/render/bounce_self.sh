#!/bin/sh
# bounce_self.sh <povray> <srcdir>: with bounce { self off } a lone cloud, glowing or not, renders as without bounce while
# its bounce lights a second cloud and a floor, alike on any thread count; an open box with a surface light keeps
# its own bounce off itself in the same way, beneath the surface and through photons too; 4.0 scenes take it.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=24; H=16
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_self.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
same() { cmp -s "$1.pixels" "$2.pixels" || { echo "bounce_self: $3" >&2; exit 1; }; }
values() { od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk '{ print $1 + $2 + $3 }'; }
sum() { values "$1" | awk '{ s += $1 } END { print s }'; }
render none +wt3
render off Declare=Bounce=2 Declare=Self=0 +wt3
render on Declare=Bounce=2 +wt3
same none off "a lone cloud without self bounce differs from one without bounce"
[ "$(sum on)" -gt "$(sum none)" ] || { echo "bounce_self: self bounce does not brighten a lone cloud" >&2; exit 1; }
render half Declare=Bounce=1 Declare=Self=0.5 +wt3
render full Declare=Bounce=1 +wt3
render tint Declare=Bounce=1 Declare=Tint=1 +wt3
# Single-order self shares are linear per channel, up to 16-bit output rounding.
od -An -v -tu2 --endian=big -w6 none.pixels > none.rgb
od -An -v -tu2 --endian=big -w6 full.pixels > full.rgb
od -An -v -tu2 --endian=big -w6 half.pixels > half.rgb
od -An -v -tu2 --endian=big -w6 tint.pixels > tint.rgb
paste none.rgb full.rgb half.rgb tint.rgb | awk '
    function abs(v) { return v < 0 ? -v : v }
    { for (c=1;c<=3;c++) if (abs($(c+6)-($c+$(c+3))/2)>1) exit 1;
      if (abs($10-$1)>1 || abs($11-$8)>1 || abs($12-$6)>1) exit 1 }' ||
    { echo "bounce_self: scalar and RGB shares do not scale the bounce independently" >&2; exit 1; }
render half2 Declare=Bounce=2 Declare=Self=0.5 +wt3
[ "$(sum half2)" -gt "$(sum half)" ] && [ "$(sum half2)" -lt "$(sum on)" ] ||
    { echo "bounce_self: a fractional share did not scale subsequent self orders" >&2; exit 1; }
render glow Declare=Emit=1 +wt3
render glowoff Declare=Emit=1 Declare=Bounce=1 Declare=Self=0 +wt3
same glow glowoff "a lone glowing cloud without self bounce differs from one without bounce"
render glowhalf Declare=Emit=1 Declare=Bounce=1 Declare=Self=0.5 +wt3
render glowon Declare=Emit=1 Declare=Bounce=1 +wt3
render modifieroff Declare=Modifier=1 Declare=Bounce=1 Declare=Self=0 +wt3
render modifierhalf Declare=Modifier=1 Declare=Bounce=1 Declare=Self=0.5 +wt3
render modifieron Declare=Modifier=1 Declare=Bounce=1 +wt3
midpoint() {
    od -An -v -tu2 --endian=big -w6 "$1.pixels" > low.rgb
    od -An -v -tu2 --endian=big -w6 "$2.pixels" > middle.rgb
    od -An -v -tu2 --endian=big -w6 "$3.pixels" > high.rgb
    paste low.rgb middle.rgb high.rgb | awk '
        function abs(v) { return v < 0 ? -v : v }
        { for (c=1;c<=3;c++) if (abs($(c+3)-($c+$(c+6))/2)>1) exit 1 }' ||
        { echo "bounce_self: $4" >&2; exit 1; }
}
# The compensation adds a second sampled emitter, so compare image energy rather than individual draws.
echo "$(sum glowoff) $(sum glowhalf) $(sum glowon)" | awk '
    { m=($1+$3)/2; d=$2-m; if (d<0) d=-d; if (!($2>$1 && $2<$3) || d>0.02*m) exit 1 }' ||
    { echo "bounce_self: fractional self changes original emission" >&2; exit 1; }
midpoint modifieroff modifierhalf modifieron "self share must follow subtraction modifiers"
render mask Declare=Scene=1 Declare=Mask=1 +wt3
# Per region, the summed pixels: the floor, then the left (bouncing) and right clouds above it.
regions() {
    values mask > mask.values; values "$1" > "$1.values"
    paste mask.values "$1.values" | awk -v w=$W '{ i = NR - 1; r = $1 > 0 ? 0 : (i % w < w / 2 ? 1 : 2); s[r] += $2 }
        END { print s[0], s[1], s[2] }'
}
render scene Declare=Scene=1 +wt3
render sceneoff Declare=Scene=1 Declare=Bounce=1 Declare=Self=0 +wt3
echo "$(regions scene) $(regions sceneoff)" | awk '{ if (!($4 > $1) || $5 != $2 || !($6 > $3)) exit 1 }' ||
    { regions scene; regions sceneoff
      echo "bounce_self: without self bounce the cloud must keep its look and light the floor and the other cloud" >&2; exit 1; }
render sceneon Declare=Scene=1 Declare=Bounce=1 +wt3
echo "$(regions sceneoff) $(regions sceneon)" | awk '{ if (!($5 > $2)) exit 1 }' ||
    { echo "bounce_self: self bounce does not brighten the bouncing cloud beside another" >&2; exit 1; }
render one Declare=Scene=1 Declare=Bounce=2 Declare=Self=0 +wt1
render four Declare=Scene=1 Declare=Bounce=2 Declare=Self=0 +wt4
same one four "a scene without self bounce differs on four threads"
# Surface lights: an open box lit inside faces itself, so its own bounce reaches it.
surface() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_self_surface.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
surface snone +wt3
surface soff Declare=Bounce=2 Declare=Self=0 +wt3
surface son Declare=Bounce=2 +wt3
same snone soff "a lone surface without self bounce differs from one without bounce"
[ "$(sum son)" -gt "$(sum snone)" ] || { echo "bounce_self: self bounce does not brighten a lone surface" >&2; exit 1; }
surface shalf Declare=Bounce=1 Declare=Self=0.5 +wt3
surface sfull Declare=Bounce=1 +wt3
surface stint Declare=Bounce=1 Declare=Tint=1 +wt3
midpoint snone shalf sfull "a surface's scalar self share does not scale its bounce on itself"
echo "$(sum snone) $(sum stint) $(sum sfull)" | awk '{ if (!($2 > $1 && $2 < $3)) exit 1 }' ||
    { echo "bounce_self: an RGB surface self share does not scale its bounce" >&2; exit 1; }
surface shalf2 Declare=Bounce=2 Declare=Self=0.5 +wt3
[ "$(sum shalf2)" -gt "$(sum shalf)" ] && [ "$(sum shalf2)" -lt "$(sum son)" ] ||
    { echo "bounce_self: a surface's fractional share did not scale its later orders" >&2; exit 1; }
surface sglow Declare=Emit=1 +wt3
surface sglowoff Declare=Emit=1 Declare=Bounce=1 Declare=Self=0 +wt3
surface sglowon Declare=Emit=1 Declare=Bounce=1 +wt3
echo "$(sum sglow) $(sum sglowoff) $(sum sglowon)" | awk '
    { d=$2-$1; if (d<0) d=-d; if (d>0.02*$1 || !($3>$2)) exit 1 }' ||
    { echo "bounce_self: a glowing surface without self bounce lost or gained its own emission" >&2; exit 1; }
surface smask Declare=Scene=1 Declare=Mask=1 +wt3
# Per region, the summed pixels: the floor, then the box.
sregions() {
    values smask > smask.values; values "$1" > "$1.values"
    paste smask.values "$1.values" | awk '{ r = $1 > 0 ? 1 : 0; s[r] += $2 } END { print s[0], s[1] }'
}
surface sscene Declare=Scene=1 +wt3
surface ssceneoff Declare=Scene=1 Declare=Bounce=1 Declare=Self=0 +wt3
surface ssceneon Declare=Scene=1 Declare=Bounce=1 +wt3
echo "$(sregions sscene) $(sregions ssceneoff) $(sregions ssceneon)" |
    awk '{ if (!($3 > $1) || $4 != $2 || $5 != $3 || !($6 > $4)) exit 1 }' ||
    { sregions sscene; sregions ssceneoff; sregions ssceneon
      echo "bounce_self: without self bounce a surface must keep its look and light the floor in full" >&2; exit 1; }
surface sone Declare=Scene=1 Declare=Bounce=2 Declare=Self=0.5 +wt1
surface sfour Declare=Scene=1 Declare=Bounce=2 Declare=Self=0.5 +wt4
same sone sfour "a surface with a self share differs on four threads"
surface ssubnone Declare=Subsurface=1 +wt3
surface ssuboff Declare=Subsurface=1 Declare=Bounce=1 Declare=Self=0 +wt3
surface ssubon Declare=Subsurface=1 Declare=Bounce=1 +wt3
same ssubnone ssuboff "a subsurface surface without self bounce differs from one without bounce"
[ "$(sum ssubon)" -gt "$(sum ssubnone)" ] || { echo "bounce_self: self bounce does not reach beneath a surface" >&2; exit 1; }
surface sphnone Declare=Photons=1 Declare=Emit=1 +wt2
surface sphoff Declare=Photons=1 Declare=Emit=1 Declare=Bounce=1 Declare=Self=0 +wt2
surface sphon Declare=Photons=1 Declare=Emit=1 Declare=Bounce=1 +wt2
echo "$(sum sphnone) $(sum sphoff) $(sum sphon)" | awk '
    { d=$2-$1; if (d<0) d=-d; if (d>0.02*$1 || !($3>$2)) exit 1 }' ||
    { echo "$(sum sphnone) $(sum sphoff) $(sum sphon)"
      echo "bounce_self: photons returning a surface's own bounce ignored its self share" >&2; exit 1; }
cat > surface.pov4 <<'EOF'
light_source { <0.3, 0.6, -0.6>, rgb 3 }
difference { box { -1, 1 } box { <-0.9, -0.9, -1.1>, <0.9, 0.9, 0.9> }
  pigment { rgb 0.8 } finish { diffuse 0.8 } light { samples 2 bounce { passes 1 self off } } }
EOF
"$POVRAY" +isurface.pov4 +w8 +h8 -d -p -f > surface.log 2>&1 ||
    { cat surface.log; echo "bounce_self: a 4.0 surface refused self off" >&2; exit 1; }
cat > scene.pov4 <<'EOF'
light_source { <0, 3, -3>, rgb 1 }
box { -1, 1 pigment { rgbt 1 } hollow
  interior { media { scattering { 1, rgb 0.5 } } light { samples 2 bounce { passes 1 self off } } } }
EOF
"$POVRAY" +iscene.pov4 +w8 +h8 -d -p -f > scene.log 2>&1 || { cat scene.log; echo "bounce_self: a 4.0 scene refused self off" >&2; exit 1; }
for scale in '0.05' '<0.1,0.03,0>' 'rgb <0.1,0.03,0>' 'on' 'off'; do
    sed "s/self off/self $scale/" scene.pov4 > accepting.pov4
    "$POVRAY" +iaccepting.pov4 +w8 +h8 -d -p -f > accepting.log 2>&1 || { cat accepting.log; exit 1; }
done
for scale in '-0.1' '1.1' '<0,1.1,0>' 'rgb <0,-0.1,0>' '1e300*1e300' '' 'passes 1' '0.5 self 0.5'; do
    sed "s/self off/self $scale/" scene.pov4 > invalid.pov4
    "$POVRAY" +iinvalid.pov4 +w8 +h8 -d -p -f > invalid.log 2>&1 &&
        { echo "bounce_self: invalid share $scale was accepted" >&2; exit 1; }
done
for photons in 0 1; do
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_self_photons.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        Declare=Photons=$photons +wt2 +o"photons$photons.ppm" > "photons$photons.log" 2>&1 ||
        { cat "photons$photons.log"; exit 1; }
    tail -c $((6 * W * H)) "photons$photons.ppm" > "photons$photons.pixels"
done
[ "$(sum photons1)" -gt "$(sum photons0)" ] ||
    { echo "bounce_self: self off removed original emission returning through photons" >&2; exit 1; }
for mode in 1 2; do
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_self_photons.pov" +w$W +h$H +pr -a -d -p -v -gp +fp16 File_Gamma=1 \
        Declare=PhotonFile=$mode +wt2 +o"saved$mode.ppm" > "saved$mode.log" 2>&1 ||
        { cat "saved$mode.log"; exit 1; }
    tail -c $((6 * W * H)) "saved$mode.ppm" > "saved$mode.pixels"
done
same saved1 saved2 "saved photons lost the source's self share"
echo "bounce_self: scalar and RGB self shares preserve other receivers and original emission"
