#!/bin/sh
# bounce_self.sh <povray> <srcdir>: with bounce { self off } a lone cloud, glowing or not, renders as without bounce while
# its bounce lights a second cloud and a floor, alike on any thread count; surface lights refuse it, 4.0 scenes take it.
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
render glow Declare=Emit=1 +wt3
render glowoff Declare=Emit=1 Declare=Bounce=1 Declare=Self=0 +wt3
same glow glowoff "a lone glowing cloud without self bounce differs from one without bounce"
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
cat > surface.pov <<'EOF'
box { 0, 1 pigment { rgb 1 } light { bounce { self off } } }
EOF
"$POVRAY" +isurface.pov +w8 +h8 -d -p -f > surface.log 2>&1 && { cat surface.log; echo "bounce_self: a surface light took self off" >&2; exit 1; }
tr -s '\n ' '  ' < surface.log | grep -q 'bounce { self off } is for interior lights, not surface lights' ||
    { cat surface.log; exit 1; }
cat > scene.pov4 <<'EOF'
light_source { <0, 3, -3>, rgb 1 }
box { -1, 1 pigment { rgbt 1 } hollow
  interior { media { scattering { 1, rgb 0.5 } } light { samples 2 bounce { passes 1 self off } } } }
EOF
"$POVRAY" +iscene.pov4 +w8 +h8 -d -p -f > scene.log 2>&1 || { cat scene.log; echo "bounce_self: a 4.0 scene refused self off" >&2; exit 1; }
echo "bounce_self: a cloud's bounce without self lights other things and leaves the cloud as it was"
